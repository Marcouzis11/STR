#include <stdio.h>
#include <time.h>
#include <unistd.h>

int main() {

    time_t inicio = time(NULL);

    printf("\n\n");

    while (1) {

        time_t ahora = time(NULL);
        int transcurrido = (int)(ahora - inicio);

        int horas = transcurrido / 3600;
        int minutos = (transcurrido % 3600) / 60;
        int segundos = transcurrido % 60;

        struct tm *info = localtime(&ahora);

        printf("\033[2A");

        printf("Cronometro: %02d:%02d:%02d\n",
            horas, minutos, segundos);

        printf("Hora actual: %02d:%02d:%02d\n",
            info->tm_hour,
            info->tm_min,
            info->tm_sec);

        fflush(stdout);
        sleep(1);
    }

    return 0;
}