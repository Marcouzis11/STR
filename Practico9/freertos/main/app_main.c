#include <stdio.h>
#include <stdlib.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#define BUTTON_GPIO GPIO_NUM_0
#define LED_GPIO GPIO_NUM_2
#define DEBOUNCE_TIME_MS 50

static SemaphoreHandle_t xEventSemaphore = NULL;
static volatile TickType_t last_event_tick = 0;
static volatile TickType_t prev_event_tick = 0;
static volatile TickType_t last_isr_tick = 0;
static volatile int led_state = 0;

static void IRAM_ATTR button_isr_handler(void* arg) {
    (void)arg;
    
    TickType_t current_tick = xTaskGetTickCountFromISR();
    TickType_t elapsed_since_last_isr = (current_tick - last_isr_tick) * portTICK_PERIOD_MS;
    
    if (elapsed_since_last_isr >= DEBOUNCE_TIME_MS) {
        prev_event_tick = last_event_tick;
        last_event_tick = current_tick;
        last_isr_tick = current_tick;
        
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xSemaphoreGiveFromISR(xEventSemaphore, &xHigherPriorityTaskWoken);
        
        if (xHigherPriorityTaskWoken == pdTRUE) {
            portYIELD_FROM_ISR();
        }
    }
}

static void processing_task(void* pvParameters) {
    (void)pvParameters;
    
    while (1) {
        if (xSemaphoreTake(xEventSemaphore, portMAX_DELAY) == pdTRUE) {
            led_state = !led_state;
            gpio_set_level(LED_GPIO, led_state);
            
            TickType_t elapsed = (last_event_tick - prev_event_tick);
            TickType_t elapsed_ms = elapsed * portTICK_PERIOD_MS;
            
            printf("[PROCESAMIENTO] LED %s | Tiempo: %lu ticks (%lu ms)\n",
                   led_state ? "ON" : "OFF",
                   (unsigned long)elapsed,
                   (unsigned long)elapsed_ms);
        }
    }
}

static void telemetry_task(void* pvParameters) {
    (void)pvParameters;
    while (1) {
        printf("[TELEMETRIA] Sistema Operativo Saludable\n");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void app_main(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE
    };
    
    gpio_config(&io_conf);
    
    io_conf.pin_bit_mask = (1ULL << LED_GPIO);
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    
    gpio_config(&io_conf);
    
    xEventSemaphore = xSemaphoreCreateBinary();
    
    if (xEventSemaphore == NULL) {
        printf("Error: No se pudo crear el semaforo binario\n");
        while (1);
    }
    
    led_state = 0;
    gpio_set_level(LED_GPIO, led_state);
    
    printf("Sistema inicializado. Esperando eventos...\n");
    
    gpio_install_isr_service(ESP_INTR_FLAG_LEVEL1);
    gpio_isr_handler_add(BUTTON_GPIO, button_isr_handler, NULL);
    
    xTaskCreatePinnedToCore(
        processing_task,
        "ProcessingTask",
        4096,
        NULL,
        3,
        NULL,
        1
    );
    
    xTaskCreatePinnedToCore(
        telemetry_task,
        "TelemetryTask",
        4096,
        NULL,
        1,
        NULL,
        1
    );
    
    vTaskDelete(NULL);
}