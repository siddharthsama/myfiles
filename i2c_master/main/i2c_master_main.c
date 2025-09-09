// File: main/i2c_master_main.c
#include <stdio.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"

#define I2C_MASTER_SCL_IO       21
#define I2C_MASTER_SDA_IO       22
#define I2C_MASTER_NUM          I2C_NUM_0
#define I2C_MASTER_FREQ_HZ      400000
#define I2C_MASTER_TIMEOUT_MS   1000

#define SLAVE_ADDR              0x28

static const char *TAG = "i2c_master";


static void i2c_master_init(i2c_master_bus_handle_t *bus_handle,
                            i2c_master_dev_handle_t *dev_handle)
{
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_MASTER_NUM,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, bus_handle));

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SLAVE_ADDR,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(*bus_handle, &dev_config, dev_handle));
}

void app_main(void)
{   
    
    i2c_master_bus_handle_t bus_handle;
    i2c_master_dev_handle_t dev_handle;
    i2c_master_init(&bus_handle, &dev_handle);
    ESP_LOGI(TAG, "I2C initialized successfully");

    vTaskDelay(pdMS_TO_TICKS(3000));  


    
static uint8_t data_to_send = 0x55;
for(int i=0;i<4;i++)   
{ esp_err_t err = i2c_master_transmit(dev_handle, &data_to_send, 1, 10000/ portTICK_PERIOD_MS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Transmit failed: %s", esp_err_to_name(err));
    }
    else {
        ESP_LOGI(TAG, "Transmitted 0x%02X", data_to_send);
    }
    }
    uint8_t received = 0;
    for(int i=0;i<4;i++)   
{
    esp_err_t err = i2c_master_receive(dev_handle, &received, 1, 1000 / portTICK_PERIOD_MS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Receive failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Received 0x%02X", received);
    }
}  
 vTaskDelay(pdMS_TO_TICKS(500)); // half second delay

}
