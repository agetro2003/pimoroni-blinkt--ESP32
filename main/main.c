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

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(example_connect());


    mqtt_app_start();

    xTaskCreate(led_task, "led_task", 2048, NULL, 1, NULL);

}