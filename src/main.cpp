#include <inttypes.h>
#include <stdio.h>
#ifdef ZW_FOOTPRINT_PROBE
#include <memory>
#endif

#include "esp_err.h"
#include "esp_log.h"
#include "esp_spi_flash.h"
#include "esp_system.h"
#include "esp_fast_boot.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "ZWUtils.hpp"
#include "ZWAppConfig.h"
#include "ZWAppUtils.hpp"

#include "AppStorage/Module.hpp"
#include "AppConfig/Module.hpp"
#include "AppEventMgr/Module.hpp"
#include "AppNetwork/Module.hpp"
#include "AppTime/Module.hpp"
#include "AppHTTPD/Module.hpp"

namespace zw::esp8266::app {
namespace {

inline constexpr char TAG[] = "Main";

#ifdef ZW_FOOTPRINT_PROBE
struct FootprintCtorProbe {
  volatile uint32_t magic;
  FootprintCtorProbe() : magic(0x5A5AA5A5U) {}
} ctor_probe_;
#endif

void _stagger_boot(void) {
#if defined(CONFIG_RESET_REASON) && (CONFIG_COLD_BOOT_STAGGER_MS > 0)
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:
    case ESP_RST_BROWNOUT:
      vTaskDelay(CONFIG_COLD_BOOT_STAGGER_MS / portTICK_PERIOD_MS);
      break;

    default: {
      // Nothing to do
    }
  }
#endif
}

inline const char* _model_name(esp_chip_model_t model) {
  switch (model) {
    case CHIP_ESP8266:
      return "ESP8266";
    case CHIP_ESP32:
      return "ESP32";
  }
  return "Unknown Chip";
}

void _greeting(void) {
  ESP_LOGI(TAG, "=============================");
  ESP_LOGI(TAG, "ZWAppliance %s", ZW_APPLIANCE_NAME);
  {
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    ESP_LOGI(TAG, "%s (%d core%s) @ %d Mhz", _model_name(chip_info.model), chip_info.cores,
             chip_info.cores > 1 ? "s" : "", CONFIG_ESP8266_DEFAULT_CPU_FREQ_MHZ);

    ESP_LOGI(TAG, "Silicon revision: %d", chip_info.revision);
    ESP_LOGI(TAG, "%s flash: %uKB",
             (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "Embedded" : "External",
             spi_flash_get_chip_size() / 1024);
  }
  ESP_LOGI(TAG, "SDK version: %s", esp_get_idf_version());
  ESP_LOGI(TAG, "Free heap memory: %" PRIu32, esp_get_free_heap_size());
  ESP_LOGI(TAG, "=============================");
}

void _reboot_event(int32_t event_id, void* event_data, void* handler_arg) {
  // Give a little time for the signaler to complete their call.
  vTaskDelay(CONFIG_FREERTOS_HZ / 2);

  // Reverse order of initialization
  ESP_LOGI(TAG, "Finalizing components...");
  httpd::finit();
  network::finit();
  time::finit();
  config::finit();
  eventmgr::finit();
  storage::finit();

  ESP_LOGI(TAG, "Rebooting...");

  if (eventmgr::system_states_peek(ZW_SYSTEM_STATE_BOOT_IMAGE_ALT)) {
#ifdef ZW_APPLIANCE_FASTBOOT
    // Disable fast-boot since we will be booting from a new image.
    esp_fast_boot_disable();
#endif
    esp_restart();
  } else {
    // Note that this always works, even if bootloader fast boot is disabled.
    esp_fast_boot_restart();
  }
}

}  // namespace

esp_err_t main(void) {
  _stagger_boot();
  _greeting();

#ifdef ZW_APPLIANCE_FASTBOOT
  esp_fast_boot_enable();
#endif

  // Order independent sub-system init
  ESP_RETURN_ON_ERROR(storage::init());
  ESP_RETURN_ON_ERROR(eventmgr::init());

  // Order dependent sub-system initialization
  ESP_RETURN_ON_ERROR(config::init());
#ifdef ZW_SYSTIME_AVAILABLE
  ESP_RETURN_ON_ERROR(time::init());
#endif
  ESP_RETURN_ON_ERROR(network::init());
  ESP_RETURN_ON_ERROR(httpd::init());

  ESP_LOGD(TAG, "** Heap: %" PRIu32 "; Stack: %u", esp_get_free_heap_size(),
           uxTaskGetStackHighWaterMark(NULL));

  // Adjust system state per config
  if (config::get()->dev_mode) {
    ESP_RETURN_ON_ERROR(storage::remount_system_rw());
  }

  // Initialization is now complete. And depending on the situation,
  // the appliance may either start regular serving or go into network
  // provisioning mode, and may cycle between them.
  // If anything goes wrong, the ZW_SYSTEM_STATE_FAILURE event will be signaled.

  ESP_RETURN_ON_ERROR(eventmgr::system_event_register_handler(
      ZW_SYSTEM_EVENT_REBOOT, eventmgr::SystemEventHandlerWrapper<TAG, _reboot_event>));

  // All modules have allocated their RTC memory: verify the salted digest, unlock R/W.
  ESP_RETURN_ON_ERROR(storage::rtcmem_alloc_complete());

  // Send the bootstrap signal to start the system.
  ESP_RETURN_ON_ERROR(eventmgr::system_event_post(ZW_SYSTEM_EVENT_BOOT_COMPLETE));

#ifdef ZW_FOOTPRINT_PROBE
  {
    int ctor_ok = (ctor_probe_.magic == 0x5A5AA5A5U) ? 1 : 0;
    int shared_ptr_ok = 1;
#if !defined(__GNUC__) || __GNUC__ >= 12
    auto sp = std::make_shared<int>(42);
    shared_ptr_ok = (sp && *sp == 42) ? 1 : 0;
#endif
    ESP_LOGI("PROBE", "[CXX_CHECK] global_ctor=%d shared_ptr=%d", ctor_ok, shared_ptr_ok);
    log_footprint_state("boot");
  }
#endif
  return ESP_OK;
}

}  // namespace zw::esp8266::app

using namespace zw::esp8266::app;

extern "C" void app_main(void) {
  ESP_GOTO_ON_ERROR(main(), failed);
  return;

failed:
  ESP_LOGE(TAG, "Appliance initialization failed!");
}