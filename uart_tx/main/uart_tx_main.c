#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "driver/spi_master.h"
#include "freertos/ringbuf.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_crt_bundle.h"
#include "ringbuf.h"
#include "esp_timer.h"  
#include "driver/gpio.h"
#include "freertos/queue.h"

#define SUCCESS_QUEUE_LEN 10   // queue length
#define SUCCESS_VAL 1
#define FAIL_VAL    0

// Queue handle
static QueueHandle_t success_queue = NULL;

// static const int RX_BUF_SIZE = 1024;

#define WIFI_SSID      "SAMA_NextGen"
#define WIFI_PASS      "mPK6EAfH"
#define MAXIMUM_RETRY  5

static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
static int s_retry_num = 0;

static int64_t http_start_time = 0;
static int64_t http_end_time   = 0;
static int64_t uart_start_time  = 0;
static int64_t uart_end_time    = 0;

#define BLOCK_SIZE   1024
#define NUM_BLOCKS   8


#define TX_UART_PORT    UART_NUM_1
#define TX_UART_TX_PIN  26
#define TX_UART_RX_PIN  25

static ringbuf_handle_t rb = NULL;


#define UART_PORT    UART_NUM_1
#define UART_BAUD    1152000
#define RX_BUF_SIZE  128


static const char *TAG = "UART_TX";

static void wifi_event_handler(void* arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGW(TAG, "Retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGW(TAG, "Connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                            pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);
}

#define MAX_HTTP_RECV_BUFFER 2048

void http_task(void *pvParameters)
{
    int http_count = 0;
    esp_http_client_config_t config = {
        .url = "https://gitlab.com/divyesh.vartha/cheap-host/-/raw/main/opus/small_harry.ogg?ref_type=heads&inline=false",
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = MAX_HTTP_RECV_BUFFER,
        .timeout_ms = 60000,
        .keep_alive_enable = true,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (esp_http_client_open(client, 0) != ESP_OK) {
        ESP_LOGE("HTTP_TASK", "HTTP open failed");
        vTaskDelete(NULL);
    }

    int64_t file_size = esp_http_client_fetch_headers(client);
    ESP_LOGI("HTTP_TASK", "File size: %lld bytes", file_size);

    uint8_t buffer[BLOCK_SIZE];
    int data_read;
    bool first_chunk = true;

    while ((data_read = esp_http_client_read(client, (char *)buffer, sizeof(buffer))) > 0) {
        if (first_chunk) {
            http_start_time = esp_timer_get_time();
            ESP_LOGI("HTTP_TASK", "HTTP first read at %lld us", http_start_time);

            // Write the first chunk into RB but only after tx_task clears query stage
            int written;
            do {
                written = rb_write(rb, (char *)buffer, data_read, portMAX_DELAY);
            } while (written <= 0);

            first_chunk = false;
        } else {
            // Normal streaming to ring buffer
            int written;
            do {
                
                written = rb_write(rb, (char *)buffer, data_read, portMAX_DELAY);

                // if(written>0){
                //     http_count++;
                //     printf("HTTP COUNT IS %d\n", http_count);
                // }
            } while (written <= 0);
        }
    }

    http_end_time = esp_timer_get_time();
    ESP_LOGI("HTTP_TASK", "HTTP transfer done in %.2f sec",
             (http_end_time - http_start_time) / 1000000.0);

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    rb_done_write(rb);
    vTaskSuspend(NULL);
}




static void uart_init(void)
{
    uart_config_t cfg = {
        .baud_rate = UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };

    const int uart_rx_buf_size = RX_BUF_SIZE * 2;
    const int uart_tx_buf_size = 1024; // give driver a TX buffer for non-blocking writes

    ESP_ERROR_CHECK(uart_driver_install(TX_UART_PORT, uart_rx_buf_size, uart_tx_buf_size, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(TX_UART_PORT, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(TX_UART_PORT, TX_UART_TX_PIN, TX_UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
}

#define FRAME_HEADER "SAMA"
#define FRAME_HEADER_LEN 4
#define FRAME_FOOTER "END"
#define FRAME_FOOTER_LEN 3


static int build_frame(uint8_t msg_type,
                       const uint8_t *payload,
                       int payload_len,
                       uint8_t *out_buf,
                       int out_buf_size)
{
    int frame_len = FRAME_HEADER_LEN + 2 /*length*/ + 1 /*msg_type*/ +
                    payload_len + FRAME_FOOTER_LEN;

    if (out_buf_size < frame_len) {
        ESP_LOGE(TAG, "build_frame: out_buf too small (need %d, have %d)", frame_len, out_buf_size);
        return -1;
    }

    uint8_t *p = out_buf;

    // Header
    memcpy(p, FRAME_HEADER, FRAME_HEADER_LEN);
    p += FRAME_HEADER_LEN;

    // Frame length (payload length only, 2 bytes LE)
    uint16_t len = payload_len;
    *p++ = len & 0xFF;
    *p++ = (len >> 8) & 0xFF;

    // Message type
    *p++ = msg_type;

    // Payload
    memcpy(p, payload, payload_len);
    p += payload_len;

    // Footer
    memcpy(p, FRAME_FOOTER, FRAME_FOOTER_LEN);
    p += FRAME_FOOTER_LEN;

    return (p - out_buf); // total size
}





// void uart_check_send(void *arg) {
//     uint8_t test_data[10] = {0x55, 0xAA, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

//     while (1) {
//     // Send 10 bytes
//     int written = uart_write_bytes(UART_PORT, (const char *)test_data, sizeof(test_data));
//     ESP_LOGI(TAG, "Sent %d bytes", written);
//     vTaskDelay(pdMS_TO_TICKS(100));
//     }
//     // // Read any bytes received back
//     // int len = uart_read_bytes(UART_PORT, rx_buf, sizeof(rx_buf), pdMS_TO_TICKS(1000));
//     // if (len > 0) {
//     //     ESP_LOGI(TAG, "Received %d bytes", len);
//     //     ESP_LOG_BUFFER_HEXDUMP(TAG, rx_buf, len, ESP_LOG_INFO);
//     // } else {
//     //     ESP_LOGI(TAG, "No response received");
//     // }
// }

// void uart_check_rec(void *arg) {

//     uint8_t rx_buf[1];

//     while (1) {

//     // Read any bytes received back
//     int len = uart_read_bytes(UART_PORT, rx_buf, sizeof(rx_buf),portMAX_DELAY);
//     if (len > 0) {
//         ESP_LOGI(TAG, "Received %d bytes and val: %02X\n", len, rx_buf[0]);
//         // ESP_LOGI(TAG, "Received Vals %02X %02X %02X\n",rx_buf[0], rx_buf[1], rx_buf[2]);
//     } else {
//         ESP_LOGI(TAG, "No response received");
//     }
// }

// }


static void tx_task(void *arg)
{
    uint8_t buffer[BLOCK_SIZE];
    uint8_t frame_buf[BLOCK_SIZE + FRAME_HEADER_LEN + 2 + 1 + FRAME_FOOTER_LEN]; // header+len+type+payload+footer
    int read;
    int count = 0;

    bool first_rx = true;


    int query_count = 0;

    // ---------- 1) Send QUERY frames until success ----------
    ESP_LOGI("TX_TASK", "Sending query frames...");
    int qry_frame_len = build_frame('Q', buffer, 1024, frame_buf, sizeof(frame_buf));

    int flag = 0;
    while (1) {
        int written = uart_write_bytes(TX_UART_PORT, (const char *)frame_buf, qry_frame_len);
        if (written > 0) {
            ESP_LOGI("TX_TASK", "Sent QUERY frame (%d bytes)", written);
            query_count++;
        }

        vTaskDelay(pdMS_TO_TICKS(200));

        if (xQueueReceive(success_queue, &flag, portMAX_DELAY) == pdTRUE && flag == SUCCESS_VAL) {
            ESP_LOGI("TX_TASK", "Received SUCCESS, starting payload transfer");
            uart_start_time = esp_timer_get_time();
            break;
        }
    }

    // ---------- 2) Send real payload from ring buffer ----------
    while (1) {

        read = rb_read(rb, (char *)buffer, sizeof(buffer), portMAX_DELAY);

        if (read > 0) {
            if(first_rx == true || (xQueueReceive(success_queue, &flag, portMAX_DELAY) == pdTRUE && flag == SUCCESS_VAL) )
        {   
             int frame_len = build_frame('A', buffer, read, frame_buf, sizeof(frame_buf));
            if (frame_len < 0) {
                ESP_LOGE("TX_TASK", "Frame buffer too small");
                continue;
            }
            first_rx = false;
            int written = uart_write_bytes(TX_UART_PORT, (const char *)frame_buf, frame_len);


                    // printf("Sent %d bytes: ", written);
                    //     for (int i = 0; i < written; i++) {
                    //         printf("%02X ", frame_buf[i]);
                    //     }
                    //     printf("\n");


            if (written < 0) {
                ESP_LOGE("TX_TASK", "uart_write_bytes failed");
            } else {
                count++;
                printf("val of count %d\n",count);
                ESP_LOGI("TX_TASK", "Sent %d bytes (payload=%d)", written, read);
            }
        }
        } 
        else if (read == RB_DONE) {
            uart_wait_tx_done(TX_UART_PORT, pdMS_TO_TICKS(2000));
            uart_end_time = esp_timer_get_time();
            ESP_LOGI("TX_TASK", "UART finished in %.2f sec",
                     (uart_end_time - uart_start_time) / 1000000.0);
            break;
        } 
        else if (read == RB_TIMEOUT) {
            ESP_LOGI("TX_TASK", "RB timeout, waiting...");
        } 
        else {
            ESP_LOGW("TX_TASK", "RB read error %d", read);
        }
    }

    // printf("QUERY COUNT IS %d\n",query_count);

    ESP_LOGI("TX_TASK", "tx_task finished");
    vTaskDelete(NULL);
}

void rx_task(void *arg)
{
    uint8_t buf[1];
    while (1) {
        int len = uart_read_bytes(TX_UART_PORT, buf, sizeof(buf), pdMS_TO_TICKS(1000));
        if (len > 0) {
            ESP_LOGI("UART_RX", "Received %d bytes: 0x%02X", len, buf[0]);
            int flag = (buf[0] == 0x07) ? SUCCESS_VAL : FAIL_VAL;

            // send result to queue
            if (xQueueSend(success_queue, &flag, 0) != pdTRUE) {
                ESP_LOGW("UART_RX", "Queue full, dropping flag");
            }
        }
        else{
        int flag = FAIL_VAL;
        xQueueSend(success_queue, &flag, 0);
        }

    }
 
    vTaskSuspend(NULL);
}



void app_main(void) {

    ESP_LOGI(TAG, "Starting example");

    esp_err_t ret = nvs_flash_init();
    
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    wifi_init_sta();

    rb = rb_create(1024, NUM_BLOCKS);
    if (!rb) {
        ESP_LOGE(TAG, "Failed to create ring buffer");
        return;
    }

    success_queue = xQueueCreate(SUCCESS_QUEUE_LEN, sizeof(int));
if (success_queue == NULL) {
    ESP_LOGE(TAG, "Failed to create success_queue");
    return;
}

    uart_init();

    // xTaskCreate(uart_check_send, "uart_check_send", 4096, NULL, 10, NULL);

    // xTaskCreate(uart_check_rec, "uart_check_rec", 4096, NULL, 10, NULL);

    xTaskCreate(http_task, "http_task", 8192, NULL, 10, NULL);
    xTaskCreate(tx_task, "tx_task", 8192, NULL, 10, NULL);
    xTaskCreate(rx_task, "rx_task", 4096, NULL, 9, NULL);
}
