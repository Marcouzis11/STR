#include <stdio.h>
#include <pigpio.h>
#include <unistd.h>

// Definición de los pines (usando numeración BCM)
#define PIN_SERVO 24 
#define PIN_BOTON 17

// Parámetros del Servo según consigna
#define PULSO_MIN 500   // 0 grados
#define PULSO_MAX 2500  // 180 grados

// Parámetros de Robustez
#define DEBOUNCE_US 200000 // 200ms convertidos a microsegundos

// Variables de estado globales
volatile int estado_actual = 0; // Máquina de estados: 0 a 6
volatile int pulso_actual = PULSO_MIN;
volatile uint32_t ultimo_tiempo_isr = 0;

// Función de Interrupción (ISR)
void isr_boton(int gpio, int level, uint32_t tick) {
    // "tick" nos da el tiempo actual en microsegundos. 
    // Comprobamos el antirrebote
    if (tick - ultimo_tiempo_isr > DEBOUNCE_US) {
        
        // Lógica de Máquina de Estados Finitos
        if (estado_actual >= 6) {
            estado_actual = 0; // Resetear a 0° si alcanzó o superó los 180°
        } else {
            estado_actual++; // Avanzar al siguiente estado (equivale a 30°)
        }

        // Calcular el pulso exacto multiplicando para no perder decimales
        pulso_actual = PULSO_MIN + (estado_actual * 2000 / 6);

        // Mover el actuador usando la función de la librería pigpio
        gpioServo(PIN_SERVO, pulso_actual);
        
        // Imprimir en terminal la posición actual
        printf("Posición actual: %d microsegundos\n", pulso_actual);

        // Actualizar el tiempo de la última pulsación válida
        ultimo_tiempo_isr = tick;
    }
}

int main() {
    // 1. Inicializar la librería pigpio
    if (gpioInitialise() < 0) {
        printf("Error al inicializar pigpio. ¿Ejecutaste con sudo?\n");
        return 1;
    }

    // 2. Configurar el Servomotor
    gpioSetMode(PIN_SERVO, PI_OUTPUT);
    gpioServo(PIN_SERVO, pulso_actual); // El servo debe iniciar en 0°
    printf("Sistema iniciado. Posición inicial: %d microsegundos\n", pulso_actual);

    // 3. Configurar el Botón
    gpioSetMode(PIN_BOTON, PI_INPUT);
    gpioSetPullUpDown(PIN_BOTON, PI_PUD_DOWN); // Configurar resistencia Pull-Down interna

    // 4. Configurar la Interrupción
    // ISR por flanco de subida, sin usar polling
    gpioSetISRFunc(PIN_BOTON, RISING_EDGE, 0, isr_boton);

    // 5. Bucle principal
    while(1) {
        sleep(1); // Mantiene el programa vivo sin consumir CPU (no es polling)
    }

    // Limpieza al salir
    gpioTerminate();
    return 0;
}