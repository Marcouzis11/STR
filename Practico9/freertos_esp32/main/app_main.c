#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#define QUEUE_SIZE 10
#define PRODUCER_DELAY_MS 500
#define QUEUE_SEND_TIMEOUT_MS 10

static QueueHandle_t xDataQueue = NULL;

static void producer_task(void* pvParameters) {
    (void)pvParameters;

    while (1) {
        int sensor_value = rand() % 101;
        
        BaseType_t result = xQueueSend(xDataQueue, &sensor_value, pdMS_TO_TICKS(QUEUE_SEND_TIMEOUT_MS));
        
        if (result == pdTRUE) {
            printf("[PRODUCTOR] Valor enviado: %d (Core: %d)\n", sensor_value, xPortGetCoreID());
        } else {
            printf("[PRODUCTOR] Cola llena, dato descartado: %d\n", sensor_value);
        }
        
        vTaskDelay(pdMS_TO_TICKS(PRODUCER_DELAY_MS));
    }
}

static void consumer_task(void* pvParameters) {
    (void)pvParameters;
    int received_value;

    while (1) {
        BaseType_t result = xQueueReceive(xDataQueue, &received_value, portMAX_DELAY);
        
        if (result == pdTRUE) {
            printf("[CONSUMIDOR] Valor recibido: %d | Core: %d | Procesando...\n", 
                   received_value, xPortGetCoreID());
        }
    }
}

void app_main(void) {
    xDataQueue = xQueueCreate(QUEUE_SIZE, sizeof(int));
    
    if (xDataQueue == NULL) {
        printf("Error: No se pudo crear la cola\n");
        while (1);
    }
    
    printf("Sistema de telemetría iniciado. Cola creada con capacidad %d\n", QUEUE_SIZE);
    
    xTaskCreatePinnedToCore(
        producer_task,
        "Productor",
        4096,
        NULL,
        2,
        NULL,
        1
    );
    
    xTaskCreatePinnedToCore(
        consumer_task,
        "Consumidor",
        4096,
        NULL,
        1,
        NULL,
        0
    );
    
    vTaskDelete(NULL);
}