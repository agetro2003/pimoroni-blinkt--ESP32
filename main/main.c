#include <stdio.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "mqtt_client.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"
#include "esp_ota_ops.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "esp_log.h"

// OTA
extern const uint8_t server_cert_pem_start[] asm("_binary_ca_cert_pem_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_ca_cert_pem_end");
#define HASH_LEN 32
#define OTA_URL_SIZE 256
static const char *TAG = "OTA";
int ota_flag=0;

esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
    case HTTP_EVENT_ERROR:
        ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
        break;
    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
        break;
    case HTTP_EVENT_ON_HEADER:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
        break;
    case HTTP_EVENT_ON_DATA:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
        break;
    case HTTP_EVENT_REDIRECT:
        ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
        break;
    }
    return ESP_OK;
}


void ota_task(void *pvParameter)
{
    while (1){
        if (ota_flag==0) {
            vTaskDelay(60000 / portTICK_PERIOD_MS); // Espera 1 minuto antes de volver a intentar
        }
        else if (ota_flag==1) {
             ESP_LOGI(TAG, "Starting OTA example task");
#ifdef CONFIG_EXAMPLE_FIRMWARE_UPGRADE_BIND_IF
    esp_netif_t *netif = get_example_netif_from_desc(bind_interface_name);
    if (netif == NULL) {
        ESP_LOGE(TAG, "Can't find netif from interface description");
        abort();
    }
    struct ifreq ifr;
    esp_netif_get_netif_impl_name(netif, ifr.ifr_name);
    ESP_LOGI(TAG, "Bind interface name is %s", ifr.ifr_name);
#endif
    esp_http_client_config_t config = {
        .url = CONFIG_FIRMWARE_UPGRADE_URL,
#ifdef CONFIG_EXAMPLE_USE_CERT_BUNDLE
        .crt_bundle_attach = esp_crt_bundle_attach,
#else
        .cert_pem = (char *)server_cert_pem_start,
#endif /* CONFIG_EXAMPLE_USE_CERT_BUNDLE */
        .event_handler = _http_event_handler,
        .keep_alive_enable = true,
#ifdef CONFIG_EXAMPLE_FIRMWARE_UPGRADE_BIND_IF
        .if_name = &ifr,
#endif
    };

#ifdef CONFIG_EXAMPLE_FIRMWARE_UPGRADE_URL_FROM_STDIN
    char url_buf[OTA_URL_SIZE];
    if (strcmp(config.url, "FROM_STDIN") == 0) {
        example_configure_stdin_stdout();
        fgets(url_buf, OTA_URL_SIZE, stdin);
        int len = strlen(url_buf);
        url_buf[len - 1] = '\0';
        config.url = url_buf;
    } else {
        ESP_LOGE(TAG, "Configuration mismatch: wrong firmware upgrade image url");
        abort();
    }
#endif

#ifdef CONFIG_EXAMPLE_SKIP_COMMON_NAME_CHECK
    config.skip_cert_common_name_check = true;
#endif

    esp_https_ota_config_t ota_config = {
        .http_config = &config,
    };
    ESP_LOGI(TAG, "Attempting to download update from %s", config.url);
    esp_err_t ret = esp_https_ota(&ota_config);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "OTA Succeed, Rebooting...");
        esp_restart();
    } else {
        ESP_LOGE(TAG, "Firmware upgrade failed");
    }
    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
            ota_flag = 0;

        }
    
}
}

static void print_sha256(const uint8_t *image_hash, const char *label)
{
    char hash_print[HASH_LEN * 2 + 1];
    hash_print[HASH_LEN * 2] = 0;
    for (int i = 0; i < HASH_LEN; ++i) {
        sprintf(&hash_print[i * 2], "%02x", image_hash[i]);
    }
    ESP_LOGI(TAG, "%s %s", label, hash_print);
}

static void http_cleanup(esp_http_client_handle_t client)
{
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
}

static void get_sha256_of_partitions(void)
{
    uint8_t sha_256[HASH_LEN] = { 0 };
    esp_partition_t partition;

    // get sha256 digest for bootloader
    partition.address   = ESP_BOOTLOADER_OFFSET;
    partition.size      = ESP_PARTITION_TABLE_OFFSET;
    partition.type      = ESP_PARTITION_TYPE_APP;
    esp_partition_get_sha256(&partition, sha_256);
    print_sha256(sha_256, "SHA-256 for bootloader: ");

    // get sha256 digest for running partition
    esp_partition_get_sha256(esp_ota_get_running_partition(), sha_256);
    print_sha256(sha_256, "SHA-256 for current firmware: ");
}


static void __attribute__((noreturn)) task_fatal_error(void)
{
    ESP_LOGE(TAG, "Exiting task due to fatal error...");
    (void)vTaskDelete(NULL);

    while (1) {
        ;
    }
}



static bool diagnostic(void)
{
    esp_err_t err;
    bool diagnostic_is_ok=false;
    esp_http_client_config_t config = {
        .url = CONFIG_FIRMWARE_UPGRADE_URL,
#ifdef CONFIG_EXAMPLE_USE_CERT_BUNDLE
        .crt_bundle_attach = esp_crt_bundle_attach,
#else
        .cert_pem = (char *)server_cert_pem_start,
#endif /* CONFIG_EXAMPLE_USE_CERT_BUNDLE */
        .event_handler = _http_event_handler,
        .keep_alive_enable = true,
#ifdef CONFIG_EXAMPLE_FIRMWARE_UPGRADE_BIND_IF
        .if_name = &ifr,
#endif
    };

    /*
    esp_http_client_config_t config = {
        .url = CONFIG_EXAMPLE_FIRMWARE_UPG_URL,
        .cert_pem = (char *)server_cert_pem_start,
        .timeout_ms = CONFIG_EXAMPLE_OTA_RECV_TIMEOUT,
        .keep_alive_enable = true,
    };
    */

#ifdef CONFIG_EXAMPLE_FIRMWARE_UPGRADE_URL_FROM_STDIN
    char url_buf[OTA_URL_SIZE];
    if (strcmp(config.url, "FROM_STDIN") == 0) {
        example_configure_stdin_stdout();
        fgets(url_buf, OTA_URL_SIZE, stdin);
        int len = strlen(url_buf);
        url_buf[len - 1] = '\0';
        config.url = url_buf;
    } else {
        ESP_LOGE(TAG, "Configuration mismatch: wrong firmware upgrade image url");
        abort();
    }
#endif

#ifdef CONFIG_EXAMPLE_SKIP_COMMON_NAME_CHECK
    config.skip_cert_common_name_check = true;
#endif

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialise HTTP connection");
        task_fatal_error();
    }
    err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        task_fatal_error();
    }
    else{
        diagnostic_is_ok = true;
    }
    esp_http_client_fetch_headers(client);

    esp_http_client_cleanup(client);
    ESP_LOGE(TAG, "Closing HTTP connection");

    return diagnostic_is_ok;
}
// FIN OTA

#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define APA_SOF 0b11100000

spi_device_handle_t spi;

//Almacen de los datos de los leds
uint32_t leds[8] = {};
// contador de los led
uint8_t count = 0;
uint8_t count_bits[8];

// Maximos valor de cuenta
uint8_t maxCount = 255;

void convert_to_bits(uint8_t c, uint8_t c_b[8]){
    for (int i = 0; i < 8; i++){
        c_b[i] = (c >> (7-i)) & 1;
    }
}

//Convertir RGB y Brillo
uint32_t rgbb(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness) {
    uint32_t result = 0;
    result |= ((uint32_t)r << 24);           //rojo
    result |= ((uint32_t)g << 16);           // verde
    result |= ((uint32_t)b << 8);            // azul
    result |= (brightness & 0b11111);        // Brillo
    return result;
}

uint32_t rgb (uint8_t r, uint8_t g, uint8_t b ){
    return rgbb(r, g, b, CONFIG_DEFAULT_BRIGHTNESS);
}

// Configura un led individual
void set_pixel(uint8_t led, uint8_t r, uint8_t g, uint8_t b) {
    if (led < 8) {
        leds[led] = rgb(r, g, b);
    }
}

void show(){
    // buffer para los datos
    uint8_t buffer[4 + 4 * 8 + (8 + 1) / 2];

    int index = 0;
    // Enviar el frame de inicio
    for (int i = 0; i < 4; i++) {
        buffer[index++] = 0x00;
    }
    // Enviar los datos de los LEDs
    for (int i = 0; i < 8; i++) {
        uint8_t brightness = APA_SOF | (leds[i] & 0x1F);
        uint8_t b = (leds[i] >> 8) & 0xFF;
        uint8_t g = (leds[i] >> 16) & 0xFF;
        uint8_t r = (leds[i] >> 24) & 0xFF;

        buffer[index++] = brightness;
        buffer[index++] = b;
        buffer[index++] = g;
        buffer[index++] = r;
    }
    // Enviar el frame de fin
    for (int i = 0; i < (8 + 1) / 2; i++) {
        buffer[index++] = 0xFF;
    }

    // Enviar el buffer completo
    spi_transaction_t t = {
        .length = index * 8,
        .tx_buffer = buffer,
    };
    spi_device_transmit(spi, &t);

}

// Borra todos los LEDs
void clear() {
    for (int i = 0; i < 8; i++) {
        leds[i] = rgb(0, 0, 0);
    }
    show();
}


// MQTT Event Handler
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    char count_str[10];
    sprintf(count_str, "%d", count);
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED: 
            msg_id = esp_mqtt_client_subscribe(client, "/maxCount", 0);
            printf("MQTT_EVENT_CONNECTED, msg_id=%d\n", msg_id);

            msg_id = esp_mqtt_client_subscribe(client, "/setCount", 0);
            printf("MQTT_EVENT_CONNECTED, msg_id=%d\n", msg_id);

            msg_id = esp_mqtt_client_subscribe(client, "/addCount", 0);
            printf("MQTT_EVENT_CONNECTED, msg_id=%d\n", msg_id);

            msg_id = esp_mqtt_client_subscribe(client, "/OTA", 0);
            printf("MQTT_EVENT_CONNECTED, msg_id=%d\n", msg_id);
            
            msg_id = esp_mqtt_client_publish(client, "/currentCount", count_str, 0, 1, 0);
            printf("MQTT_EVENT_PUBLISHED, msg_id=%d\n", msg_id);

            break;
        case MQTT_EVENT_DISCONNECTED:
            printf("MQTT_EVENT_DISCONNECTED\n");
            break;
        case MQTT_EVENT_SUBSCRIBED:
            printf("MQTT_EVENT_SUBSCRIBED\n");
            msg_id = esp_mqtt_client_publish(client, "/currentCount", count_str, 0, 1, 0);
            printf("MQTT_EVENT_PUBLISHED, msg_id=%d\n", msg_id);
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            printf("MQTT_EVENT_UNSUBSCRIBED\n");
            break;
        case MQTT_EVENT_PUBLISHED:
            printf("MQTT_EVENT_PUBLISHED\n");
            break;
        case MQTT_EVENT_DATA:
            printf("MQTT_EVENT_DATA\n");
            if (strncmp(event->topic, "/maxCount", event->topic_len)==0){
                char data[16] = {0};
                memcpy(data, event->data, MIN(event->data_len, sizeof(data)-1));
                int newMaxCount = atoi(data);
                if (newMaxCount > 255) maxCount = 255;
                else maxCount = newMaxCount;
                if (count > maxCount) count = maxCount;
            } else if (strncmp(event->topic, "/setCount", event->topic_len)==0){
                char data[16] = {0};
                memcpy(data, event->data, MIN(event->data_len, sizeof(data)-1));
                int newCount = atoi(data);
                if (newCount > maxCount) {
                    count = maxCount;
                 } else if (newCount > 255) {
                    count = 255;
                } else {
                    count = newCount;
                }
            } else if (strncmp(event->topic, "/addCount", event->topic_len)==0){
                char data[16] = {0};
                memcpy(data, event->data, MIN(event->data_len, sizeof(data)-1));
                int addValue = atoi(data);
                int newCount = count + addValue;
                if (newCount > maxCount) count = maxCount;
                else count = newCount;
            } else if (strncmp(event->topic, "/OTA", event->topic_len)==0){
            char data[16] = {0};
            memcpy(data, event->data, MIN(event->data_len, sizeof(data)-1));
            int senhal_OTA= atoi(data);
            if(senhal_OTA==1){
                //diagnostico();
                printf("ACTUALIZA POR OTA\n");
                ota_flag=1;
            }
        }
             else {
                printf("Unknown topic: %.*s\n", event->topic_len, event->topic);
            }
           
            sprintf(count_str, "%d", count);
            esp_mqtt_client_publish(client, "/currentCount", count_str, 0, 1, 0);
            break;
        case MQTT_EVENT_ERROR:
            printf("MQTT_EVENT_ERROR\n");
            break;
        default:
        printf("Other event id: %" PRId32 "\n", event_id);
        break;


    }
}

// MQTT Initialization
static void mqtt_app_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = CONFIG_BROKER_URL,
    };
    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
}
// Tarea para parpadear los LEDs
void led_task(void *pvParameter) {
    while (1) {
        printf("LED Task Running...\n");
       convert_to_bits(count, count_bits);
        for (int i = 0; i<8; i++){
           // printf("%d %d \n", count_bits[i], (count_bits[i]==1) );
           // color rojo si ya se alcanzo el maximo, verde si no
            (count_bits[i] == 1) ? 
            
            ((count >= maxCount) ? set_pixel(i, 255, 0, 0) : set_pixel(i, 0, 255, 0))
            
            : 
             
             set_pixel(i, 0,0,0); 
       }
       show();
        vTaskDelay(1000 / portTICK_PERIOD_MS); // Espera 1 segundo
    }
}

void apa102_init() {
    
    // Initialize the SPI bus 
    spi_bus_config_t buscfg = {
        .miso_io_num = -1,
        .mosi_io_num = CONFIG_GPIO_MOSI,
        .sclk_io_num = CONFIG_GPIO_CLOCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4 * 8 * 8 + 4, // 4 bytes for start frame, 8 LEDs, 4 bytes for end frame
    };

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 1000000, // Clock out at 1 MHz
        .mode = 0, // SPI mode 0
        .spics_io_num = -1,
        .queue_size = 1
    };
    spi_bus_initialize(HSPI_HOST, &buscfg, SPI_DMA_DISABLED);
    spi_bus_add_device(HSPI_HOST, &devcfg, &spi);

}


void app_main(void)
{
    apa102_init();
    clear();
    printf("Starting Project ...");
    printf("Brillo: %d\n", CONFIG_DEFAULT_BRIGHTNESS);
    
    get_sha256_of_partitions();

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(example_connect());


    mqtt_app_start();
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;

//    xTaskCreate(led_task, "led_task", 2048, NULL, 1, NULL);
   /* if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY){
            if(diagnostic()){
                ESP_LOGI(TAG, "Diagnostics completed successfully! Continuing execution ...");
                esp_ota_mark_app_valid_cancel_rollback();
            } else {
                ESP_LOGE(TAG, "Diagnostics failed! Start rollback to the previous version ...");
                esp_ota_mark_app_invalid_rollback_and_reboot();
            }
    }    
}*/
    xTaskCreatePinnedToCore(ota_task, "ota_task", 8192, NULL, 6, NULL, 0);
    xTaskCreatePinnedToCore(led_task, "main_task", 4096, NULL, 4, NULL, 1);
}