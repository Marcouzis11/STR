#include <stdio.h>
#include <pigpio.h>
#include <unistd.h>

#define LED_PIN 17     // GPIO para el LED
#define BUTTON_PIN 18  // GPIO para el botón
#define DEBOUNCE_US 20000 // Tiempo de debounce en microsegundos (5 ms)

// Callback que se ejecuta cuando el botón cambia de estado (Apretar o soltar el botón)
void button_callback(int gpio, int level, uint32_t tick) {
    if (level == 0) { // Botón presionado = Prender led
        gpioWrite(LED_PIN, 1);
        printf("BOTÓN_PRESIONADO\n");
    } else if (level == 1) { // Botón soltado = Apagar led
        gpioWrite(LED_PIN, 0);
        printf("BOTÓN_LIBERADO\n");
    }
}

int main() {
    if (gpioInitialise() < 0) { //Se usa siempre al inicio del programa para inicializar la biblioteca pigpio. Devuelve un valor negativo si hay un error.
        printf("Error inicializando pigpio\n");
        return 1;
    }

    // Configurar LED como salida
    gpioSetMode(LED_PIN, PI_OUTPUT);

    // Configurar botón como entrada
    gpioSetMode(BUTTON_PIN, PI_INPUT);
    gpioSetPullUpDown(BUTTON_PIN, PI_PUD_UP); // Configurar el boton para que si no esta precionado vale 0 y si lo preciono vale 1

    gpioGlitchFilter(BUTTON_PIN, DEBOUNCE_US); // Configurar un filtro de rebote para el botón (Ignorar cambios rápidos en el estado del botón)
    
    // Registrar la función de interrupción (Si el botón cambia de estado, se llama a button_callback)
    gpioSetAlertFunc(BUTTON_PIN, button_callback);

    // Mantener el programa corriendo indefinidamente
    while (1) {
        sleep(1);
    }

    gpioTerminate(); //Va siempre y es para liberar los recursos de pigpio antes de salir del programa. {En este caso, Nunca se ejecuta por el while}
    return 0;
}