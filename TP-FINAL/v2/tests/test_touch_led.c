#include <stdio.h>
#include <unistd.h>
#include "gpio_hal.h"
#include "config.h"

#define DEBOUNCE_MS     50
#define POLL_US         10000

int main(void) {
    printf("=== Test Touch Boton + LED Rojo ===\n");
    printf("TOCA el sensor: LED Rojo (GPIO13) se enciende.\n");
    printf("SUELTALO: LED Rojo se apaga.\n");
    printf("Ctrl+C para salir.\n\n");

    int h = gpio_open();
    if (h < 0) {
        printf("ERROR: gpio_open\n");
        return 1;
    }

    gpio_claim_input(h, TTP223B_GPIO);
    gpio_claim_output(h, LED_RED_GPIO, 0);

    bool led_on = false;
    bool last_raw = false;

    while (1) {
        bool raw = (gpio_read(h, TTP223B_GPIO) == 0);

        if (raw != last_raw) {
            usleep(DEBOUNCE_MS * 1000);
            bool confirmed = (gpio_read(h, TTP223B_GPIO) == 0);

            if (confirmed == raw) {
                if (confirmed && !led_on) {
                    gpio_write(h, LED_RED_GPIO, 1);
                    led_on = true;
                    printf("  TOCADO  -> LED ON\n");
                } else if (!confirmed && led_on) {
                    gpio_write(h, LED_RED_GPIO, 0);
                    led_on = false;
                    printf("  SUELTO  -> LED OFF\n");
                }
            }
        }

        last_raw = raw;
        usleep(POLL_US);
    }

    gpio_close(h);
    return 0;
}
