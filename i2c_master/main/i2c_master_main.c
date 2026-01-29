#include "driver/i2c_master.h"
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

#define FRAME_HEADER "SAMA"
#define FRAME_HEADER_LEN 4
#define FRAME_FOOTER "END"
#define FRAME_FOOTER_LEN 3

// Queue handle
static QueueHandle_t success_queue = NULL;


#define WIFI_SSID      "SAMA_NextGen"
#define WIFI_PASS      "mPK6EAfH"
#define MAXIMUM_RETRY  5

static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
static int s_retry_num = 0;

static int64_t http_start_time = 0;
static int64_t http_end_time   = 0;

static int64_t i2c_start_time  = 0;
static int64_t i2c_end_time    = 0;

#define BLOCK_SIZE   512
#define NUM_BLOCKS   80



#define I2C_MASTER_SCL_IO       19
#define I2C_MASTER_SDA_IO       18
#define I2C_MASTER_NUM          I2C_NUM_0
#define I2C_MASTER_FREQ_HZ      400000
#define I2C_MASTER_TIMEOUT_MS   1000
#define I2C_SLAVE_ADDR          0x42


static ringbuf_handle_t rb = NULL;

volatile bool tx_ready = false;

i2c_master_bus_handle_t bus_handle;
i2c_master_dev_handle_t dev_handle;


static const char *TAG = "I2C_TX";

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
        } else if(tx_ready){
            // Normal streaming to ring buffer
            int written;
            do {
                   if(rb_is_full(rb)){
                    vTaskDelay(pdMS_TO_TICKS(100));
                }

                written = rb_write(rb, (char *)buffer, data_read, portMAX_DELAY);

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
        .device_address = I2C_SLAVE_ADDR,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(*bus_handle, &dev_config, dev_handle));
}



static int build_frame(uint8_t msg_type,
                       const uint8_t *payload,
                       int payload_len,
                       uint8_t *out_buf,
                       int out_buf_size)
{
    int frame_len = FRAME_HEADER_LEN + 1 /*msg_type*/ + 2 /*length*/ +
                    payload_len + FRAME_FOOTER_LEN;

    if (out_buf_size < frame_len) {
        ESP_LOGE(TAG, "build_frame: out_buf too small (need %d, have %d)", frame_len, out_buf_size);
        return -1;
    }

    uint8_t *p = out_buf;

    // Header
    memcpy(p, FRAME_HEADER, FRAME_HEADER_LEN);
    p += FRAME_HEADER_LEN;

    // Message type
    *p++ = msg_type;

    // Frame length (payload length only, 2 bytes LE)
    uint16_t len = payload_len;
        if(payload_len !=0){
        *p++ = len & 0xFF;
        *p++ = (len >> 8) & 0xFF;
        }

    

    // Payload
    if(payload_len !=0){
    memset(p, 0, payload_len);
    memcpy(p, payload, payload_len);
    p += payload_len;
    }
    // Footer
    memcpy(p, FRAME_FOOTER, FRAME_FOOTER_LEN);
    p += FRAME_FOOTER_LEN;

    return (p - out_buf); // total size
}

static void i2c_tx_task(void *arg)
{
    
    bool sync_done = false;

    uint8_t buffer[BLOCK_SIZE];
    uint8_t frame_buf[BLOCK_SIZE + FRAME_HEADER_LEN + 2 + 1 + FRAME_FOOTER_LEN];
    int read, count = 0;

    ESP_LOGI("I2C_TX", "Starting I2C frame transmission...");

    // ---------- Send QUERY Frame ----------
    int qry_frame_len = build_frame('Q', buffer, 0, frame_buf, sizeof(frame_buf));

    if(!tx_ready){
    while (1) {
        
        esp_err_t err;
        err = i2c_master_transmit(dev_handle, frame_buf, qry_frame_len,
                                            I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
        if (err == ESP_OK) {
            ESP_LOGI("I2C_TX", "Sent QUERY frame (%d bytes)", qry_frame_len);
        } else {
            ESP_LOGE("I2C_TX", "Failed to send QUERY: %s", esp_err_to_name(err));
            continue;
        }

        // Immediately read ACK from slave
        uint8_t ack = 0;
        err = i2c_master_receive(dev_handle, &ack, 1,
                                 I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);

        if (err == ESP_OK)
        {
            if (ack == 0x07)
            {
                ESP_LOGI("I2C_TX", "Received success ACK — starting payload transfer");
                tx_ready = true;
                i2c_start_time = esp_timer_get_time();

                break;
            } else {
                ESP_LOGI("I2C_TX", "ACK fail Received %x", ack);
            }
        }
        else {
                ESP_LOGW("I2C_TX", "Did not receive success ACK, retrying...");
        }

            vTaskDelay(pdMS_TO_TICKS(200));

    }
        
}
    // ---------- Send Payload Frames ----------
    while (1) {

        ESP_LOGI("I2C_TX", "INSIDE PAYLOAD TRANSFER");

        esp_err_t err;
        read = rb_read(rb, (char *)buffer, sizeof(buffer), portMAX_DELAY);
        if (read > 0) {
            int frame_len = build_frame('A', buffer, 64, frame_buf, sizeof(frame_buf));
            if (frame_len < 0) {
                ESP_LOGE("I2C_TX", "Frame buffer too small");
                continue;
            }

            // Transmit payload frame
            esp_err_t err = i2c_master_transmit(dev_handle, frame_buf, frame_len,
                                                I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
            if (err != ESP_OK) {
                ESP_LOGE("I2C_TX", "Transmit failed: %s", esp_err_to_name(err));
                continue;
            }

            // Immediately read ACK
            uint8_t ack = 0;
            err = i2c_master_receive(dev_handle, &ack, 1,
                                     I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
            if (err == ESP_OK && ack == 0x07) {
                count++;
                ESP_LOGI("I2C_TX", "Sent frame %d (%d bytes payload)", count, read);
            } else {
                ESP_LOGW("I2C_TX", "Payload frame not ACKed, retrying...");
                vTaskDelay(pdMS_TO_TICKS(50)); // optional backoff
            }
        } else if (read == RB_DONE) {
            // End-of-stream frame
            int end_frame_len = build_frame('E', buffer, 0, frame_buf, sizeof(frame_buf));
            err = i2c_master_transmit(dev_handle, frame_buf, end_frame_len,
                                      I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
            if (err == ESP_OK)
                ESP_LOGI("I2C_TX", "Sent END frame");

            // Wait for final success ACK
            uint8_t ack = 0;
            err = i2c_master_receive(dev_handle, &ack, 1,
                                     I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
            if (err == ESP_OK && ack == 0x07) {
                ESP_LOGI("I2C_TX", "Received END ACK");
                break;
            } else {
                ESP_LOGW("I2C_TX", "END frame not ACKed, retrying...");
                vTaskDelay(pdMS_TO_TICKS(50));
            }
        }
    }

    i2c_end_time = esp_timer_get_time();
    ESP_LOGI("I2C_TX", "I2C transfer complete (%.2f s)",
             (i2c_end_time - i2c_start_time) / 1e6);

    vTaskDelete(NULL);


}


void app_main(void) {

    ESP_LOGI(TAG, "Starting ESP I2C Streamer");

    ESP_ERROR_CHECK(nvs_flash_init());
    wifi_init_sta();

    rb = rb_create(1024, NUM_BLOCKS);
    if (!rb) {
        ESP_LOGE(TAG, "Failed to create ring buffer");
        return;
    }

    success_queue = xQueueCreate(SUCCESS_QUEUE_LEN, sizeof(int));
    if (!success_queue) {
        ESP_LOGE(TAG, "Failed to create success_queue");
        return;
    }

 
            esp_err_t ret;

            // Initialize the I2C bus
            i2c_master_bus_config_t bus_config = {
                .i2c_port = I2C_NUM_0,
                .sda_io_num = I2C_MASTER_SDA_IO,
                .scl_io_num = I2C_MASTER_SCL_IO,
                .clk_source = I2C_CLK_SRC_DEFAULT,
                .glitch_ignore_cnt = 7,
                .flags.enable_internal_pullup = true,
            };
            ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &bus_handle));

            i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = I2C_SLAVE_ADDR,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_config, &dev_handle));

            // // Probe a device at address 0x28

            for (uint8_t addr = 1; addr < 127; addr++){
            ret = i2c_master_probe(bus_handle, addr, 1000);
            if (ret == ESP_OK) {
                ESP_LOGI("I2C", "Device found at address %d\n",addr);
            } 
            }


    // HTTP producer
    xTaskCreate(http_task, "http_task", 8192, NULL, 10, NULL);
    // I2C transmitter and receiver
    xTaskCreate(i2c_tx_task, "i2c_tx_task", 8192, NULL, 10, NULL);
}
