#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"

#define SWITCH_GPIO GPIO_NUM_18 
#define ESP_INTR_FLAG_DEFAULT 0

static QueueHandle_t switch_evt_queue = NULL;

static void IRAM_ATTR switch_isr_handler(void* arg)
{
    uint32_t gpio_num = (uint32_t) arg;
    xQueueSendFromISR(switch_evt_queue, &gpio_num, NULL);
}

static void switch_task(void* arg)
{
    uint32_t io_num;
    int state = 0;

    while (1) {
        if (xQueueReceive(switch_evt_queue, &io_num, portMAX_DELAY)) {
            int level = gpio_get_level(io_num);
            printf("Button pressed, level: %d\n", level);

            if (level == 0) { 
                state = !state;
                printf("Button pressed, toggled state to: %d\n", state);
            }
        }
    }
}

void app_main(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << SWITCH_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLDOWN_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    gpio_config(&io_conf);

    switch_evt_queue = xQueueCreate(10, sizeof(uint32_t));

    xTaskCreate(switch_task, "switch_task", 2048, NULL, 10, NULL);


    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
    gpio_isr_handler_add(SWITCH_GPIO, switch_isr_handler, (void*) SWITCH_GPIO);

    printf("Switch interrupt example started. Listening on GPIO %d\n", SWITCH_GPIO);
}
