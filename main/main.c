#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"

#define APA_SOF 0b11100000


spi_device_handle_t spi;

//Almacen de los datos de los leds
uint32_t leds[8] = {};
uint8_t count = 0;
uint8_t count_bits[8];

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

// Envia un byte por SPI
void send_byte(uint8_t byte) {
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &byte,
    };
    spi_device_transmit(spi, &t);
}


// Envia start frame (4 bytes de 0)
void send_start_frame() {
    for (int i = 0; i < 4; i++) {
        send_byte(0x00);
    }
}

// Envia end frame (4 bytes de 0xFF)
void send_end_frame() {
    // Enviar suficientes 1s para propagar los datos (1 byte cada 2 LEDs)
    for (int i = 0; i < (8 + 1) / 2; i++) {
        send_byte(0xFF);
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

    /*
    send_start_frame();
    for (int i = 0; i < 8; i++) {
        uint8_t brightness = APA_SOF | (leds[i] & 0x1F);
        uint8_t b = (leds[i] >> 8) & 0xFF;
        uint8_t g = (leds[i] >> 16) & 0xFF;
        uint8_t r = (leds[i] >> 24) & 0xFF;
        
        send_byte(brightness);
        send_byte(b);
        send_byte(g);
        send_byte(r);

    
    }
    send_end_frame();
    */
}

// Borra todos los LEDs
void clear() {
    for (int i = 0; i < 8; i++) {
        leds[i] = rgb(0, 0, 0);
    }
    show();
}


// Tarea para parpadear los LEDs
void led_task(void *pvParameter) {
    while (1) {
        printf("LED Task Running...\n");
        /*
        leds[0] = rgb(255, 0, 0); // Rojo
        leds[1] = rgb(0, 255, 0); // Verde
        leds[2] = rgb(0, 0, 255); // Azul
        leds[3] = rgb(255, 255, 0); // Amarillo
        leds[4] = rgb(0, 255, 255); // Cian
        leds[5] = rgb(255, 0, 255); // Magenta
        leds[6] = rgb(255, 255, 255); // Blanco
        leds[7] = rgb(255, 0, 0); // Rojo
        show();
        */
       /*
       for (int i = 0; i < 8; i++) {
            set_pixel(i, 255, 0, 0); // Rojo
        }
        show();
        vTaskDelay(1000 / portTICK_PERIOD_MS); // Espera 1 segundo
        clear();
        vTaskDelay(1000 / portTICK_PERIOD_MS); // Espera 1 segundo
        for (int i = 0; i < 4; i++) {
            set_pixel(i, 0, 255, 0); // Verde
        }
        for (int i = 4; i < 6; i++) {
            set_pixel(i, 255, 255, 255); // Verde
        }
        for (int i = 6; i < 8; i++) {
            set_pixel(i, 0, 0, 0); // Verde
        }
        show();

        vTaskDelay(1000 / portTICK_PERIOD_MS); // Espera 1 segundo
        clear();
        for (int i = 0; i < 8; i++) {
            set_pixel(i, 255, 255, 0); // Azul
        }
        show();
        

        vTaskDelay(1000 / portTICK_PERIOD_MS); // Espera 1 segundo
        clear();
        */
       convert_to_bits(count, count_bits);
       for (int i = 0; i<8; i++){
            printf("%d %d \n", count_bits[i], (count_bits[i]==1) );
            (count_bits[i] == 1) ? set_pixel(i, 255, 0, 0) : set_pixel(i, 0,0,0); 
       }
       show();
        vTaskDelay(1000 / portTICK_PERIOD_MS); // Espera 1 segundo
        count++;
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

    xTaskCreate(led_task, "led_task", 2048, NULL, 1, NULL);
}