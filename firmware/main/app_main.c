// Jetta gauge display: firmware entry point.
//
// Tasks (all memory static or allocated once at boot):
//   can_rx  core 0, prio 10  TWAI receive -> queue
//   diag    core 0, prio 8   jd_diag: transports, scheduler, store (the only code that can transmit)
//   ui      core 1, prio 3   LVGL gauges, redraws only what changed
//   sysmon  core 0, prio 1   CPU/RAM headroom log
#include "can_twai.h"
#include "demo.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "jd/diag.h"
#include "jd/guard.h"
#include "sdkconfig.h"
#include "sysmon.h"
#include "ui.h"

static const char *TAG = "app";

#if CONFIG_JD_MODE_OBD
#define MODE JD_MODE_OBD
#elif CONFIG_JD_MODE_TP20
#define MODE JD_MODE_TP20
#else
#define MODE JD_MODE_PASSIVE
#endif

#if CONFIG_JD_CAN_TX_ENABLE
#define TX_BUILD_ENABLED true
#else
#define TX_BUILD_ENABLED false
#endif

#define TICK_MS        10
#define STATUS_MS      100
#define LOG_MS         10000

static QueueHandle_t s_rxq;
static jd_txgate_t   s_gate;
static jd_diag_t     s_diag;
static bool          s_can_ok;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

// Belt and braces: refuse to run if the allowlist would pass an obviously forbidden frame.
static void guard_self_test(void)
{
    const jd_frame_t forbidden[] = {
        jd_frame(0x7E0, (const uint8_t[]){ 0x01, 0x04, 0, 0, 0, 0, 0, 0 }, 8),          // clear DTCs
        jd_frame(0x7DF, (const uint8_t[]){ 0x02, 0x01, 0x05, 0, 0, 0, 0, 0 }, 8),       // functional
        jd_frame(0x740, (const uint8_t[]){ 0x10, 0x00, 0x03, 0x14, 0xFF, 0x00 }, 6),    // KWP clear DTCs
        jd_frame(0x740, (const uint8_t[]){ 0x10, 0x00, 0x02, 0x27, 0x01 }, 5),          // security access
    };
    for (size_t i = 0; i < sizeof forbidden / sizeof forbidden[0]; i++) {
        if (jd_guard_check(&forbidden[i])) {
            ESP_LOGE(TAG, "GUARD SELF-TEST FAILED (case %u): refusing to start", (unsigned)i);
            abort();
        }
    }
}

static void diag_task(void *arg)
{
    (void)arg;
    uint32_t last_tick = 0, last_status = 0, last_log = 0;
    jd_halt_reason_t last_halt = JD_HALT_NONE;
    jd_frame_t f;
    for (;;) {
        if (xQueueReceive(s_rxq, &f, pdMS_TO_TICKS(TICK_MS)) == pdTRUE) {
            do jd_diag_on_frame(&s_diag, &f, now_ms());
            while (xQueueReceive(s_rxq, &f, 0) == pdTRUE);
        }
        uint32_t now = now_ms();
        if (now - last_tick >= TICK_MS) {
            last_tick = now;
            jd_diag_tick(&s_diag, now);
        }
        if (now - last_status < STATUS_MS) continue;
        last_status = now;

        jd_bus_status_t bus = { 0 };
        can_stats_t st = { 0 };
        if (s_can_ok) {
            can_bus_status(&bus, &st);
            if (s_gate.tx_enabled) jd_txgate_bus_status(&s_gate, &bus);
        }
        if (s_gate.halt != last_halt) {
            last_halt = s_gate.halt;
            ESP_LOGE(TAG, "TRANSMIT HALTED: %s (TEC %lu, REC %lu). Reboot to retry.", jd_halt_name(s_gate.halt),
                     (unsigned long)bus.tx_errors, (unsigned long)bus.rx_errors);
        }
        jd_link_t link = jd_diag_link(&s_diag, now);
        ui_snapshot_t snap = {
            .store = s_diag.store, .link = link, .mode = MODE, .tx_enabled = s_gate.tx_enabled,
            .can_ok = s_can_ok, .halt = s_gate.halt, .rx_frames = st.rx_frames, .replies = s_diag.replies,
            .tx_sent = s_gate.sent, .rx_errors = st.rx_errors, .tx_errors = st.tx_errors,
        };
#if CONFIG_JD_DEMO
        snap.demo = true;
        demo_fill(&snap.store, now); // fake values only on screen: diag keeps its real store
#endif
        ui_publish(&snap);

        if (now - last_log >= LOG_MS) {
            last_log = now;
            ESP_LOGI(TAG, "%s | rx %lu (dropped %lu, missed %lu) | replies %lu | tx %lu refused %lu | "
                          "TEC %lu REC %lu bus errors %lu",
                     jd_link_name(link), (unsigned long)st.rx_frames, (unsigned long)st.rx_dropped,
                     (unsigned long)st.rx_missed, (unsigned long)s_diag.replies, (unsigned long)s_gate.sent,
                     (unsigned long)(s_gate.refused_guard + s_gate.refused_halted + s_gate.hw_failed),
                     (unsigned long)st.tx_errors, (unsigned long)st.rx_errors, (unsigned long)st.bus_errors);
        }
    }
}

void app_main(void)
{
    guard_self_test();
    bool tx = TX_BUILD_ENABLED && MODE != JD_MODE_PASSIVE;
#if CONFIG_JD_DEMO
    ESP_LOGW(TAG, "DEMO MODE: the screen shows FAKE values");
#endif
    ESP_LOGW(TAG, "Jetta display: mode %s, transmit %s", MODE == JD_MODE_OBD ? "OBD" : MODE == JD_MODE_TP20 ? "TP2.0" : "PASSIVE",
             tx ? "ENABLED (allowlist only)" : "OFF (listen-only)");

    s_rxq = xQueueCreate(64, sizeof(jd_frame_t));
    jd_txgate_init(&s_gate, tx, can_hw_send, NULL);
    jd_diag_init(&s_diag, MODE, &s_gate, now_ms());

    ui_start();
    esp_err_t e = can_start(tx, s_rxq);
    s_can_ok = e == ESP_OK;
    if (!s_can_ok) ESP_LOGE(TAG, "CAN start failed: %s", esp_err_to_name(e));

    xTaskCreatePinnedToCore(diag_task, "diag", 4096, NULL, 8, NULL, 0);
    sysmon_start();
}
