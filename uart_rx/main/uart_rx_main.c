#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "string.h"

#define RX_UART_PORT    UART_NUM_0
#define RX_UART_RX_PIN  3
#define RX_UART_TX_PIN  (UART_PIN_NO_CHANGE)

static const int RX_BUF_SIZE = 1024;

static const char *TAG = "UART_RX";

// static uart_isr_handle_t rx_handle = NULL;
// static QueueHandle_t i2c_event_queue = NULL;


void rx_task(void *arg) {
    uint8_t data[ RX_BUF_SIZE + 1];
    while (true) {
        int len = uart_read_bytes(RX_UART_PORT, data, RX_BUF_SIZE, pdMS_TO_TICKS(10000));
        if (len > 0) {
            data[len] = 0;
            if (strcmp((char *)data, "Yes") == 0) {
                ESP_LOGI(TAG, "Yes Received");
            } else if (strcmp((char *)data, "No") == 0) {
                ESP_LOGI(TAG, "No Received");
            } else {
                ESP_LOGI(TAG, "Please use correct input");
            }
        }
    }
}

void app_main(void) {
    uart_config_t cfg = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };

    ESP_ERROR_CHECK(uart_driver_install(RX_UART_PORT,  RX_BUF_SIZE * 2, 0, 0, NULL, 0));

    ESP_ERROR_CHECK(uart_param_config(RX_UART_PORT, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(RX_UART_PORT, RX_UART_TX_PIN, RX_UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    xTaskCreate(rx_task, "rx_task", 2048, NULL, 10, NULL);
}
