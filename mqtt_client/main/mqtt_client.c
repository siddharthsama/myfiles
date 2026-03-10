
/* FreeRTOS */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Logging */
#include "esp_log.h"

#include "nvs_flash.h"

/* I2S driver */
#include "driver/i2s_std.h"

/* Bluetooth controller */
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"

/* A2DP profile */
#include "esp_a2dp_api.h"

/* Optional but recommended (connection handling) */
#include "esp_gap_bt_api.h"
#include "freertos/task.h"
#include "esp_bt.h"
#include <stdio.h>
#include <string.h>
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "math.h"

#define I2S_LRCLK GPIO_NUM_25
#define I2S_BCLK  GPIO_NUM_26
#define I2S_DOUT  GPIO_NUM_22

static const char *TAG = "wav";

extern const uint8_t _binary_3_wav_start[];
extern const uint8_t _binary_3_wav_end[];

    i2s_chan_handle_t tx;


typedef struct {
    char riff[4];
    uint32_t size;
    char wave[4];
    char fmt[4];
    uint32_t fmt_size;
    uint16_t format;
    uint16_t channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
} wav_header_t;

    void bt_app_a2d_data_cb(const uint8_t *data, uint32_t len)
{
    size_t written;

    i2s_channel_write(tx, data, len, &written, portMAX_DELAY);
}

// static const char *TAG = "A2DP";

void bt_app_a2d_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param)
{
    switch (event) {

    case ESP_A2D_CONNECTION_STATE_EVT:
        if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
            ESP_LOGI(TAG, "A2DP connected");
        } else if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
            ESP_LOGI(TAG, "A2DP disconnected");
        }
        break;

    case ESP_A2D_AUDIO_STATE_EVT:
        if (param->audio_stat.state == ESP_A2D_AUDIO_STATE_STARTED) {
            ESP_LOGI(TAG, "Audio streaming started");
        } else if (param->audio_stat.state == ESP_A2D_AUDIO_STATE_STOPPED) {
            ESP_LOGI(TAG, "Audio streaming stopped");
        }
        break;

    case ESP_A2D_AUDIO_CFG_EVT:
        ESP_LOGI(TAG, "Audio configuration received");
        break;

    default:
        break;
    }
}

void app_main()
{
    const uint8_t *wav_start = _binary_3_wav_start;
    const uint8_t *wav_end   = _binary_3_wav_end;

    ESP_ERROR_CHECK(nvs_flash_init());

    wav_header_t *header = (wav_header_t *)wav_start;

    ESP_LOGI(TAG, "Sample rate: %ld", header->sample_rate);
    ESP_LOGI(TAG, "Channels: %d", header->channels);
    ESP_LOGI(TAG, "Bits: %d", header->bits_per_sample);

    /* Audio starts after header + "data" chunk */
    const uint8_t *audio_data = wav_start + 44;
    size_t audio_size = wav_end - audio_data;


    i2s_chan_config_t chan_cfg =
        I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);

    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx, NULL));

    i2s_std_config_t std_cfg = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(44100),

    .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(
        I2S_DATA_BIT_WIDTH_16BIT,
        I2S_SLOT_MODE_STEREO
    ),

    .gpio_cfg = {
        .mclk = I2S_GPIO_UNUSED,
        .bclk = I2S_BCLK,
        .ws   = I2S_LRCLK,
        .dout = I2S_DOUT,
        .din  = I2S_GPIO_UNUSED,
    }
    };

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(tx));

    // size_t written;

    esp_bt_controller_mem_release(ESP_BT_MODE_BLE);

esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT));

ESP_ERROR_CHECK(esp_bluedroid_init());
ESP_ERROR_CHECK(esp_bluedroid_enable());

esp_bt_dev_set_device_name("ESP32 Speaker");

esp_bt_gap_set_scan_mode(
    ESP_BT_CONNECTABLE,
    ESP_BT_GENERAL_DISCOVERABLE
);

esp_a2d_register_callback(bt_app_a2d_cb);
esp_a2d_sink_register_data_callback(bt_app_a2d_data_cb);

esp_a2d_sink_init();

    const uint8_t *ptr = audio_data;
    size_t remaining = audio_size;

    int16_t mono;
    int16_t stereo[2];
    size_t written;



    // while (remaining > 0)
    // {
    //     memcpy(&mono, ptr, 2);

    //     stereo[0] = mono;  // left
    //     stereo[1] = mono;  // right

    //     i2s_channel_write(tx, stereo, 4, &written, portMAX_DELAY);

    //     ptr += 2;
    //     remaining -= 2;
    // }

   ESP_LOGI(TAG, "Bluetooth speaker ready");

while (true) {
    vTaskDelay(pdMS_TO_TICKS(1000));
}
}