#include "can_twai.h"
#include "driver/twai.h"
#include "esp_log.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "can";
static QueueHandle_t s_rx_queue;
static volatile uint32_t s_rx_frames, s_rx_dropped;
static bool s_normal;

static void rx_task(void *arg)
{
    (void)arg;
    twai_message_t m;
    for (;;) {
        if (twai_receive(&m, portMAX_DELAY) != ESP_OK) continue;
        if (m.extd || m.rtr) continue; // diagnostic CAN here is 11-bit data frames only
        jd_frame_t f = jd_frame((uint16_t)m.identifier, m.data, m.data_length_code);
        s_rx_frames++;
        if (xQueueSend(s_rx_queue, &f, 0) != pdTRUE) s_rx_dropped++;
    }
}

esp_err_t can_start(bool normal_mode, QueueHandle_t rx_queue)
{
    s_rx_queue = rx_queue;
    s_normal = normal_mode;
    twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(CONFIG_JD_TWAI_TX_GPIO, CONFIG_JD_TWAI_RX_GPIO,
                                                          normal_mode ? TWAI_MODE_NORMAL : TWAI_MODE_LISTEN_ONLY);
    g.rx_queue_len = 32;
    g.tx_queue_len = normal_mode ? 4 : 0;
    twai_timing_config_t t = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    esp_err_t e = twai_driver_install(&g, &t, &f);
    if (e != ESP_OK) return e;
    e = twai_start();
    if (e != ESP_OK) return e;
    xTaskCreatePinnedToCore(rx_task, "can_rx", 3072, NULL, 10, NULL, 0);
    ESP_LOGW(TAG, "TWAI 500 kbit/s on TX=%d RX=%d, %s", CONFIG_JD_TWAI_TX_GPIO, CONFIG_JD_TWAI_RX_GPIO,
             normal_mode ? "NORMAL (transmit allowed through the gate)" : "LISTEN-ONLY (cannot transmit)");
    return ESP_OK;
}

bool can_hw_send(void *ctx, const jd_frame_t *f)
{
    (void)ctx;
    if (!s_normal) return false;
    twai_message_t m = { .identifier = f->id, .data_length_code = f->len };
    memcpy(m.data, f->data, f->len);
    return twai_transmit(&m, 0) == ESP_OK; // never block the diag task
}

void can_bus_status(jd_bus_status_t *out, can_stats_t *stats)
{
    twai_status_info_t s;
    if (twai_get_status_info(&s) != ESP_OK) {
        *out = (jd_bus_status_t){ .bus_off = true };
        return;
    }
    out->bus_off = s.state == TWAI_STATE_BUS_OFF || s.state == TWAI_STATE_RECOVERING;
    out->error_passive = s.tx_error_counter >= 128 || s.rx_error_counter >= 128;
    out->tx_errors = s.tx_error_counter;
    out->rx_errors = s.rx_error_counter;
    if (stats) {
        stats->rx_frames = s_rx_frames;
        stats->rx_dropped = s_rx_dropped;
        stats->rx_missed = s.rx_missed_count;
        stats->rx_overrun = s.rx_overrun_count;
        stats->tx_errors = s.tx_error_counter;
        stats->rx_errors = s.rx_error_counter;
        stats->bus_errors = s.bus_error_count;
    }
}
