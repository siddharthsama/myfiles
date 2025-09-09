#include <stdio.h>
#include "driver/i2c.h"
#include "driver/i2c_slave.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#define I2C_SLAVE_ADDR       0x28
#define I2C_SLAVE_PORT       I2C_NUM_0
#define I2C_SLAVE_SDA_IO     22
#define I2C_SLAVE_SCL_IO     21
#define I2C_RX_BUF_LEN       128
#define I2C_TX_BUF_LEN       128

static const char *TAG = "I2C_SLAVE";

// Global handle
static i2c_slave_dev_handle_t slave_handle = NULL;
static QueueHandle_t i2c_event_queue = NULL;

// Data structure for queue message
typedef struct {
    uint8_t data[4];
    size_t length;
} i2c_tx_message_t;

// ISR callback — safe: just queue a message
static bool IRAM_ATTR on_receive_cb(i2c_slave_dev_handle_t handle, const i2c_slave_rx_done_event_data_t *event, void *user_ctx)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    // Example payload
    i2c_tx_message_t msg = {
        .data = {0xEE, 0xAD, 0xBE, 0xEF},
        .length = 4
    };

    // Send message to queue (from ISR)
    xQueueSendFromISR(i2c_event_queue, &msg, &xHigherPriorityTaskWoken);

    return xHigherPriorityTaskWoken;  // Let FreeRTOS handle context switch
}

// Task to handle slave I2C write
void i2c_slave_task(void *arg)
{
    i2c_tx_message_t msg;

    while (1) {
        // Wait for ISR to send message
        if (xQueueReceive(i2c_event_queue, &msg, portMAX_DELAY)) {
            uint32_t written = 0;
            esp_err_t err = i2c_slave_write(slave_handle, msg.data, msg.length, &written, pdMS_TO_TICKS(100));

            if (err != ESP_OK) {
                ESP_LOGE(TAG, "i2c_slave_write failed: %s", esp_err_to_name(err));
            } else {
                ESP_LOGI(TAG, "Sent %d bytes to master", written);
            }
        }
    }
}

// App entry point
void app_main(void)
{
    // Create event queue
    i2c_event_queue = xQueueCreate(10, sizeof(i2c_tx_message_t));
    if (i2c_event_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create queue");
        return;
    }

    i2c_slave_config_t slave_cfg = {
        .i2c_port = I2C_SLAVE_PORT,
        .sda_io_num = I2C_SLAVE_SDA_IO,
        .scl_io_num = I2C_SLAVE_SCL_IO,
        .slave_addr = I2C_SLAVE_ADDR,
        .addr_bit_len = I2C_ADDR_BIT_LEN_7,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .send_buf_depth = I2C_TX_BUF_LEN,
        .receive_buf_depth = I2C_RX_BUF_LEN,
        .intr_priority = 0,
        .flags = {
            .allow_pd = 0,
            .enable_internal_pullup = 1,
        }
    };

    ESP_ERROR_CHECK(i2c_new_slave_device(&slave_cfg, &slave_handle));
    ESP_LOGI(TAG, "I2C slave device created");

    i2c_slave_event_callbacks_t callbacks = {
        .on_receive = on_receive_cb,
        .on_request = NULL,
    };

    ESP_ERROR_CHECK(i2c_slave_register_event_callbacks(slave_handle, &callbacks, NULL));
    ESP_LOGI(TAG, "I2C receive callback registered");

    // Create the slave task
    xTaskCreate(i2c_slave_task, "i2c_slave_task", 4096, NULL, 10, NULL);
}
