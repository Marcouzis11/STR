/*
 * latencia_boton.c
 * Medición de latencia y jitter – Raspberry Pi 3B+ V1.4
 *
 * Mide el tiempo entre el evento del botón (interrupción GPIO)
 * y el encendido efectivo del LED, calculando estadísticas.
 *
 * Compilar:
 *   gcc -o latencia_boton latencia_boton.c -lpigpio -lpthread -lm
 *
 * Ejecutar:
 *   sudo ./latencia_boton
 *
 * Prueba de carga (en otra terminal SSH):
 *   stress --cpu 4 --io 2 --vm 2 --vm-bytes 128M
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <signal.h>
#include <time.h>
#include <math.h>
#include <pigpio.h>

/* ── Configuración ─────────────────────────────────────────── */
#define LED_PIN      17
#define BUTTON_PIN   18
#define DEBOUNCE_US  20000       /* Filtro anti-rebote: 20ms    */
#define MAX_MUESTRAS 100         /* Máximo de pulsaciones       */
#define MIN_MUESTRAS 20          /* Mínimo requerido            */
/* ─────────────────────────────────────────────────────────── */

/* ── Obtener tiempo actual en microsegundos ─────────────────*/
static int64_t tiempo_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000LL + ts.tv_nsec / 1000LL;
}

/* ── Variables compartidas entre callback y main ────────────*/
static volatile int64_t t_evento   = 0;   /* timestamp del botón    */
static volatile int     nuevo_dato = 0;   /* flag: hay dato nuevo   */

static int64_t latencias[MAX_MUESTRAS];   /* array de resultados    */
static int     n_muestras = 0;            /* contador de muestras   */

/* ── Callback del botón ─────────────────────────────────────
 * Se ejecuta cuando el botón cambia de estado.
 * SOLO registra el timestamp — el LED se enciende en main()
 * para poder medir la latencia real entre ambos instantes.
 * ─────────────────────────────────────────────────────────── */
void button_callback(int gpio, int level, uint32_t tick)
{
    if (level == 0) {                      /* botón presionado       */
        t_evento   = tiempo_us();          /* T1: instante del evento */
        nuevo_dato = 1;                    /* avisa al main()        */
    } else {
        gpioWrite(LED_PIN, 0);
        printf("  BOTÓN_LIBERADO\n\n");
    }
}

/* ── Imprimir tabla de resultados ───────────────────────────*/
void imprimir_tabla(void)
{
    int64_t suma = 0, minima = latencias[0], maxima = latencias[0];

    printf("\n");
    printf("╔══════════════════════════════════════╗\n");
    printf("║     TABLA DE LATENCIAS (µs)          ║\n");
    printf("╠═══════════╦══════════════════════════╣\n");
    printf("║ Pulsación ║ Latencia (µs)            ║\n");
    printf("╠═══════════╬══════════════════════════╣\n");

    for (int i = 0; i < n_muestras; i++) {
        printf("║    %3d    ║ %10lld µs            ║\n",
                i + 1, (long long)latencias[i]);

        suma += latencias[i];
        if (latencias[i] < minima) minima = latencias[i];
        if (latencias[i] > maxima) maxima = latencias[i];
    }

    double promedio = (double)suma / n_muestras;
    int64_t jitter  = maxima - minima;

    /* Desviación estándar */
    double varianza = 0;
    for (int i = 0; i < n_muestras; i++) {
        double diff = (double)latencias[i] - promedio;
        varianza += diff * diff;
    }
    double desvio = sqrt(varianza / n_muestras);

    printf("╠═══════════╩══════════════════════════╣\n");
    printf("║  ESTADÍSTICAS                        ║\n");
    printf("╠══════════════════════════════════════╣\n");
    printf("║  Muestras  : %-5d                   ║\n", n_muestras);
    printf("║  Mínima    : %-10lld µs           ║\n", (long long)minima);
    printf("║  Máxima    : %-10lld µs           ║\n", (long long)maxima);
    printf("║  Promedio  : %-10.1f µs           ║\n", promedio);
    printf("║  Jitter    : %-10lld µs           ║\n", (long long)jitter);
    printf("║  Desvío    : %-10.1f µs           ║\n", desvio);
    printf("╚══════════════════════════════════════╝\n\n");
}

/* ── Guardar resultados en archivo CSV ──────────────────────*/
void guardar_csv(void)
{
    FILE *f = fopen("latencias.csv", "w");
    if (!f) {
        printf("Advertencia: no se pudo guardar el CSV.\n");
        return;
    }

    fprintf(f, "pulsacion,latencia_us\n");
    for (int i = 0; i < n_muestras; i++) {
        fprintf(f, "%d,%lld\n", i + 1, (long long)latencias[i]);
    }
    fclose(f);
    printf("Resultados guardados en: latencias.csv\n");
}

/* ── Handler Ctrl+C: mostrar resultados parciales ──────────*/
void sigint_handler(int sig)
{
    (void)sig;
    printf("\n\nInterrumpido por el usuario.\n");
    if (n_muestras >= MIN_MUESTRAS) {
        imprimir_tabla();
        guardar_csv();
    } else {
        printf("Solo %d muestras registradas (mínimo %d).\n",
               n_muestras, MIN_MUESTRAS);
        if (n_muestras > 0) {
            imprimir_tabla();
            guardar_csv();
        }
    }
    gpioWrite(LED_PIN, 0);
    gpioTerminate();
    exit(0);
}

/* ── main ───────────────────────────────────────────────────*/
int main(void)
{
    signal(SIGINT, sigint_handler);

    if (gpioInitialise() < 0) {
        fprintf(stderr, "Error inicializando pigpio.\n"
                        "Ejecutar con: sudo ./latencia_boton\n");
        return 1;
    }

    gpioSetMode(LED_PIN,    PI_OUTPUT);
    gpioSetMode(BUTTON_PIN, PI_INPUT);
    gpioSetPullUpDown(BUTTON_PIN, PI_PUD_UP);
    gpioGlitchFilter(BUTTON_PIN, DEBOUNCE_US);
    gpioSetAlertFunc(BUTTON_PIN, button_callback);

    printf("╔══════════════════════════════════════╗\n");
    printf("║   MEDICIÓN DE LATENCIA Y JITTER      ║\n");
    printf("║   Raspberry Pi 3B+                   ║\n");
    printf("╠══════════════════════════════════════╣\n");
    printf("║  LED    → GPIO%d (Pin 11)             ║\n", LED_PIN);
    printf("║  Botón  → GPIO%d (Pin 12)             ║\n", BUTTON_PIN);
    printf("║  Mínimo → %d pulsaciones             ║\n", MIN_MUESTRAS);
    printf("╠══════════════════════════════════════╣\n");
    printf("║  Presioná el botón %2d veces...       ║\n", MIN_MUESTRAS);
    printf("╚══════════════════════════════════════╝\n\n");

    /*
     * Bucle principal:
     * Espera que el callback ponga nuevo_dato=1,
     * luego enciende el LED y mide T2 - T1 = latencia.
     */
    while (n_muestras < MAX_MUESTRAS) {

        /* Esperar evento del botón */
        while (!nuevo_dato) {
            /* busy-wait intencional para minimizar latencia adicional */
        }
        nuevo_dato = 0;

        /* T2: instante de encendido del LED */
        int64_t t_led = tiempo_us();
        gpioWrite(LED_PIN, 1);

        /* Calcular y registrar latencia */
        int64_t latencia = t_led - t_evento;
        latencias[n_muestras] = latencia;
        n_muestras++;

        printf("  [%2d/%d] BOTÓN_PRESIONADO → Latencia: %lld µs\n",
               n_muestras, MIN_MUESTRAS, (long long)latencia);

        /* Avisar cuando se alcanza el mínimo */
        if (n_muestras == MIN_MUESTRAS) {
            printf("\n  ✓ Mínimo alcanzado. Podés seguir o presionar\n");
            printf("    Ctrl+C para ver los resultados.\n\n");
        }
    }

    /* Si llegó a MAX_MUESTRAS */
    imprimir_tabla();
    guardar_csv();

    gpioWrite(LED_PIN, 0);
    gpioTerminate();
    return 0;
}