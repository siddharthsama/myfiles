#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const int RX_BUF_SIZE = 1024;


#define TX_UART_PORT    UART_NUM_1
#define TX_UART_TX_PIN  18
#define TX_UART_RX_PIN  (UART_PIN_NO_CHANGE)

static const char *TAG = "UART_TX";

void tx_task(void *arg) {
    const char *msg = "Hello from ESP32 A!";
    while (true) {
        int len = uart_write_bytes(TX_UART_PORT, msg, strlen(msg));
        if(len>0)
        ESP_LOGI(TAG, "Sent %d bytes", len);
        
        vTaskDelay(pdMS_TO_TICKS(10000));
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

    ESP_ERROR_CHECK(uart_driver_install(TX_UART_PORT, RX_BUF_SIZE * 2, 0, 0, NULL, 0));

    ESP_ERROR_CHECK(uart_param_config(TX_UART_PORT, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(TX_UART_PORT, TX_UART_TX_PIN, TX_UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    
    xTaskCreate(tx_task, "tx_task", 2048, NULL, 10, NULL);
}
