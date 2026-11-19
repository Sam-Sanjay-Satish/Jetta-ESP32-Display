#include "sysmon.h"
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "sysmon";
#define MAX_TASKS 24

static void sysmon_task(void *arg)
{
    (void)arg;
    static TaskStatus_t tasks[MAX_TASKS];
    uint32_t prev_idle[2] = { 0 }, prev_total = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(CONFIG_JD_SYSMON_PERIOD_S * 1000));
        configRUN_TIME_COUNTER_TYPE total = 0;
        UBaseType_t n = uxTaskGetSystemState(tasks, MAX_TASKS, &total);
        uint32_t idle[2] = { 0 };
        for (UBaseType_t i = 0; i < n; i++) {
            if (strncmp(tasks[i].pcTaskName, "IDLE", 4) == 0 && tasks[i].xCoreID < 2)
                idle[tasks[i].xCoreID] += (uint32_t)tasks[i].ulRunTimeCounter;
        }
        uint32_t dt = (uint32_t)total - prev_total;
        if (prev_total && dt) {
            // run-time counters tick per core, so each core's idle share is idle/dt
            unsigned busy0 = 100 - (unsigned)((uint64_t)(idle[0] - prev_idle[0]) * 100 / dt);
            unsigned busy1 = 100 - (unsigned)((uint64_t)(idle[1] - prev_idle[1]) * 100 / dt);
            ESP_LOGI(TAG, "CPU busy: core0 %u%%, core1 %u%% | internal RAM free %u KB (min %u KB, largest %u KB)",
                     busy0, busy1,
                     (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                     (unsigned)(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024),
                     (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024));
            for (UBaseType_t i = 0; i < n; i++) {
                const char *nm = tasks[i].pcTaskName;
                if (!strcmp(nm, "diag") || !strcmp(nm, "ui") || !strcmp(nm, "can_rx") || !strcmp(nm, "sysmon"))
                    ESP_LOGI(TAG, "  task %-7s stack free %u bytes", nm, (unsigned)tasks[i].usStackHighWaterMark);
            }
        }
        prev_total = (uint32_t)total;
        prev_idle[0] = idle[0];
        prev_idle[1] = idle[1];
    }
}

void sysmon_start(void)
{
#if CONFIG_JD_SYSMON_PERIOD_S > 0
    xTaskCreatePinnedToCore(sysmon_task, "sysmon", 3072, NULL, 1, NULL, 0);
#endif
}
