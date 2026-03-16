#include <stdio.h>
#include <pigpio.h>
#include <unistd.h>

#define LED_PIN 17  // GPIO donde está conectado el LED
#define TIMER_PERIOD 500

void timer_callback(void *arg) {
    static int state = 0;
    state = !state; // Alterna entre 0 y 1 (apagado y encendido)
    gpioWrite(LED_PIN, state);//Pasarle el estado al pin del LED para que se encienda o apague
    if (state)
        printf("LED_ON\n");
    else
        printf("LED_OFF\n");
}

int main() {
    if (gpioInitialise() < 0) { //Se usa siempre al inicio del programa para inicializar la biblioteca pigpio. Devuelve un valor negativo si hay un error.
        printf("Error inicializando pigpio\n");
        return 1;
    }

    gpioSetMode(LED_PIN, PI_OUTPUT);//Aca digo que el led es una salida (Para que parpade)

    // Configura un timer no bloqueante cada 500 ms
    gpioSetTimerFunc(0, TIMER_PERIOD , timer_callback);

    // Mantener el programa corriendo indefinidamente
    while (1) {
        sleep(1);
    }

    gpioTerminate();//Va siempre y es para liberar los recursos de pigpio antes de salir del programa. {En este caso, Nunca se ejecuta por el while}
    return 0;
}