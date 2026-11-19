#ifndef ZWAPP_FACILITIES
#define ZWAPP_FACILITIES

#include <inttypes.h>

#include "esp_log.h"
#include "esp_system.h"
#ifdef ZW_FOOTPRINT_PROBE
#include "esp_heap_caps.h"
#endif

#include "FreeRTOS.h"
#include "freertos/timers.h"
#include "freertos/task.h"

#include "AppEventMgr/Interface.hpp"

namespace zw::esp8266::app {

template <const char* TAG, void (*func)(TimerHandle_t)>
void ZWTimerWrapper(TimerHandle_t timer) {
  if (!eventmgr::IsSystemFailed()) {
    func(timer);
    ESP_LOGD(TAG, "-> Heap: %" PRIu32 "; Stack: %u", esp_get_free_heap_size(),
             uxTaskGetStackHighWaterMark(NULL));
  }
}

template <const char* TAG, void (*func)(void*)>
void ZWTaskWrapper(void* param) {
  if (!eventmgr::IsSystemFailed()) {
    func(param);
    ESP_LOGD(TAG, "=> Heap: %" PRIu32 "; Stack: %u", esp_get_free_heap_size(),
             uxTaskGetStackHighWaterMark(NULL));
  }
  vTaskDelete(xTaskGetCurrentTaskHandle());
}

#ifdef ZW_FOOTPRINT_PROBE
inline void log_footprint_state(const char* state) {
  // free_8bit / min_free_8bit cover only the DRAM (8-bit / DMA capable) heap region;
  // free / min_free also include the IRAM heap region (MALLOC_CAP_32BIT only).
  ESP_LOGI("PROBE",
           "[FOOTPRINT] state=%s free=%" PRIu32 " min_free=%" PRIu32
           " free_32bit=%u free_8bit=%u min_free_8bit=%u",
           state, esp_get_free_heap_size(), esp_get_minimum_free_heap_size(),
           heap_caps_get_free_size(MALLOC_CAP_32BIT), heap_caps_get_free_size(MALLOC_CAP_8BIT),
           heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));
}
#endif

}  // namespace zw::esp8266::app

#endif  // ZWAPP_FACILITIES