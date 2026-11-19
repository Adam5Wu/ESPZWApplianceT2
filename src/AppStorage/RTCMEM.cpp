#include <algorithm>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <vector>

#include "FreeRTOS.h"
#include "freertos/semphr.h"

#include "esp_crc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "rtc.h"

#include "ZWUtils.hpp"
#include "ZWAppConfig.h"

#include "Interface.hpp"
#include "Interface_Private.hpp"

#define RTC_MEM_STRONG_INTEGRITY 0

#if RTC_MEM_STRONG_INTEGRITY
#include "rom/md5_hash.h"
#define RTC_MEM_DIGEST_LEN 16
#else
#define RTC_MEM_DIGEST_LEN 4
#endif

#define RTC_MEM_USER_SIZE (RTC_USER_SIZE - RTC_MEM_DIGEST_LEN)
#define RTC_MEM_DIGEST_ADDR (RTC_USER_BASE + RTC_MEM_USER_SIZE)

namespace zw::esp8266::app::storage {
namespace {

inline constexpr char TAG[] = "Storage:RTCMEM";

xSemaphoreHandle rtcmem_lock_ = NULL;
size_t rtc_alloc_top_ = RTC_USER_BASE;
uint32_t rtc_schema_salt_ = 0;
uint32_t rtc_alloc_count_ = 0;
bool rtc_layout_finalized_ = false;
std::vector<uint32_t> rtc_seen_names_;

void _rtcmem_digest(void *holder) {
#if RTC_MEM_STRONG_INTEGRITY
  MD5Context md5_context;
  MD5Init(&md5_context);
  MD5Update(&md5_context, (const unsigned char *)&rtc_schema_salt_, sizeof(rtc_schema_salt_));
  MD5Update(&md5_context, (const unsigned char *)RTC_USER_BASE, RTC_MEM_USER_SIZE);
  MD5Final((unsigned char *)holder, &md5_context);
#else
  *(uint32_t *)holder =
      crc32_le(rtc_schema_salt_, (const uint8_t *)RTC_USER_BASE, RTC_MEM_USER_SIZE);
#endif
  ESP_LOG_BUFFER_HEX_LEVEL(TAG, holder, RTC_MEM_DIGEST_LEN, ESP_LOG_DEBUG);
}

bool _rtcmem_integrity_check() {
  char digest[RTC_MEM_DIGEST_LEN];
  _rtcmem_digest(digest);
  return memcmp(digest, (void *)RTC_MEM_DIGEST_ADDR, RTC_MEM_DIGEST_LEN) == 0;
}

}  // namespace

const void *RTCAlloc::data() const {
  if (!rtc_layout_finalized_) {
    ESP_LOGE(TAG, "RTC read before layout finalized");
    abort();
  }
  return base_;
}

esp_err_t RTCAlloc::update(const void *in, size_t size) {
  if (!rtc_layout_finalized_) return ESP_ERR_INVALID_STATE;
  // Must not roll over the end
  if ((uint32_t)base_ + size > RTC_MEM_DIGEST_ADDR) return ESP_ERR_INVALID_SIZE;
  // We will wait forever, don't expect failure
  xSemaphoreTake(rtcmem_lock_, portMAX_DELAY);
  memcpy(base_, in, size);
  // Must re-compute the digest
  _rtcmem_digest((void *)RTC_MEM_DIGEST_ADDR);
  xSemaphoreGive(rtcmem_lock_);
  return ESP_OK;
}

esp_err_t RTCRawData::update(const void *in, size_t size) {
  // Must not go beyond the allocated size
  if (size > size_) return ESP_ERR_INVALID_SIZE;
  return RTCAlloc::update(in, size);
}

RTCRawData rtcmem_alloc_raw(size_t size, uint32_t name_salt, uint32_t type_salt) {
  if (rtc_layout_finalized_) {
    ESP_LOGE(TAG, "RTC allocation after layout finalized");
    return RTCRawData(nullptr, 0);
  }
  if (std::find(rtc_seen_names_.begin(), rtc_seen_names_.end(), name_salt) !=
      rtc_seen_names_.end()) {
    ESP_LOGE(TAG, "Duplicate RTC allocation name salt 0x%08" PRIx32, name_salt);
    return RTCRawData(nullptr, 0);
  }
  rtc_seen_names_.push_back(name_salt);

  // Access is 4-byte aligned, so round up to a 4-byte boundary.
  size_t aligned = (size + 3) & ~size_t(3);
  if (aligned == 0 || rtc_alloc_top_ + aligned > RTC_MEM_DIGEST_ADDR) {
    return RTCRawData(nullptr, 0);
  }

  struct {
    uint32_t index;
    uint32_t raw_size;
    uint32_t aligned_size;
    uint32_t name_salt;
    uint32_t type_salt;
  } desc{rtc_alloc_count_++, static_cast<uint32_t>(size), static_cast<uint32_t>(aligned), name_salt,
         type_salt};
  rtc_schema_salt_ =
      crc32_le(rtc_schema_salt_, reinterpret_cast<const uint8_t *>(&desc), sizeof(desc));

  RTCRawData result((void *)rtc_alloc_top_, aligned);
  rtc_alloc_top_ += aligned;
  return result;
}

esp_err_t rtcmem_init(void) {
  rtc_alloc_top_ = RTC_USER_BASE;
  rtc_schema_salt_ = 0;
  rtc_alloc_count_ = 0;
  rtc_layout_finalized_ = false;
  rtc_seen_names_.clear();

  ESP_LOGI(TAG, "Total %d bytes, %d allocable", RTC_USER_SIZE, RTC_MEM_USER_SIZE);

  ESP_RETURN_ON_ERROR(
      (rtcmem_lock_ = xSemaphoreCreateMutex(), rtcmem_lock_ == NULL ? ESP_FAIL : ESP_OK));

  return ESP_OK;
}

esp_err_t rtcmem_finalize_layout(void) {
  if (rtc_layout_finalized_) return ESP_ERR_INVALID_STATE;

  rtc_seen_names_.clear();
  rtc_seen_names_.shrink_to_fit();

  xSemaphoreTake(rtcmem_lock_, portMAX_DELAY);
  if (_rtcmem_integrity_check()) {
    ESP_LOGI(TAG, "Integrity check passed (%" PRIu32 " allocs, %u B, salt 0x%08" PRIx32 ")",
             rtc_alloc_count_, static_cast<unsigned>(rtc_alloc_top_ - RTC_USER_BASE),
             rtc_schema_salt_);
    ESP_LOGD(TAG, "Dump of RTC memory (%d bytes):", RTC_USER_SIZE);
    ESP_LOG_BUFFER_HEXDUMP(TAG, (void *)RTC_USER_BASE, RTC_USER_SIZE, ESP_LOG_DEBUG);
  } else {
    ESP_LOGI(TAG, "Digest mismatch, re-initializing...");
    memset((void *)RTC_USER_BASE, 0, RTC_MEM_USER_SIZE);
    _rtcmem_digest((void *)RTC_MEM_DIGEST_ADDR);
  }
  rtc_layout_finalized_ = true;
  xSemaphoreGive(rtcmem_lock_);
  return ESP_OK;
}

void rtcmem_finit(void) {
  // Take the RTC memory lock, to avoid incomplete updates.
  xSemaphoreTake(rtcmem_lock_, portMAX_DELAY);
}

// Compile-time regression checks for recursive RTC type reflection (zero runtime cost).
namespace {
struct ReflS1 {
  int32_t a, b;
};
struct ReflS2 {
  float a, b;
};
struct ReflArr {
  char tag[8];
  uint16_t port;
};
struct ReflBootRecordLike {
  struct timeval last_known;
};
static_assert(detail::type_salt<ReflS1>() != detail::type_salt<ReflS2>(),
              "Same-size structs with different field types must produce different salts");
static_assert(detail::field_count<ReflArr>() == 2,
              "Array member must not brace-elide during field_count");
static_assert(detail::type_salt<ReflBootRecordLike>() !=
                  detail::mix_u32(detail::mix_u32(0x52544331u, sizeof(ReflBootRecordLike)),
                                  alignof(ReflBootRecordLike)),
              "Recursive type_salt must differ from 2a size/align salt");
}  // namespace

}  // namespace zw::esp8266::app::storage
