/*
 * Sistema de Control de Barrera Crítica — Raspberry Pi 3 Model B+
 * Programación Concurrente - Ing. Storaccio Luis
 *
 * Hardware requerido:
 *   - Servo SG90 o similar  → GPIO 18 (PWM hardware, Pin 12)
 *   - Botón STOP            → GPIO 27 (Pin 13) + GND
 *   - LED advertencia       → GPIO 17 (Pin 11) + resistencia 330Ω + GND
 *
 * Dependencias:
 *   sudo apt install pigpio
 *
 * Compilar:
 *   gcc -o barrera barrera_control.c -lpthread -lrt -lpigpio -Wall -std=gnu11
 *
 * Ejecutar (requiere root para pigpio y SCHED_FIFO):
 *   sudo ./barrera
 */

#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <stdatomic.h>
#include <string.h>
#include <errno.h>
#include <pigpio.h>

/* =========================================================
 * PINES GPIO (numeración BCM)
 * ========================================================= */
#define GPIO_SERVO   27   /* PWM hardware — Pin físico 12 */
#define GPIO_BOTON   17   /* Botón STOP   — Pin físico 13 */
#define GPIO_LED     18   /* LED alerta   — Pin físico 11 */

/* =========================================================
 * CONSTANTES DEL SERVO
 *
 * pigpio controla el servo con ancho de pulso en µs:
 *   500  µs →   0°
 *   1500 µs →  90°
 *   2500 µs → 180°
 * ========================================================= */
#define SERVO_MIN_GRADOS   0
#define SERVO_MAX_GRADOS   180
#define SERVO_INCREMENTO   1          /* grados por paso (movimiento suave) */
#define SERVO_DELAY_US     15000      /* 15 ms entre pasos ≈ 2.7 s por barrido */

#define PULSO_MIN_US       500
#define PULSO_MAX_US       2500

/* Macro: convierte grados → µs de pulso PWM */
#define GRADOS_A_PULSO(g) \
    (PULSO_MIN_US + ((g) * (PULSO_MAX_US - PULSO_MIN_US) / SERVO_MAX_GRADOS))

/* =========================================================
 * ESTADO GLOBAL COMPARTIDO (variables atómicas)
 *
 * parada_emergencia:
 *   0 = sistema normal
 *   2 = evento pendiente (señalado por callback_boton)
 *   1 = parada activa (procesada por hilo de seguridad)
 * ========================================================= */
static atomic_int servo_posicion    = 0;
static atomic_int parada_emergencia = 0;
static atomic_int sistema_activo    = 1;

/* =========================================================
 * PROTOTIPOS
 * ========================================================= */
void* hilo_monitoreo_seguridad(void* arg);
void  handler_telemetria(int sig, siginfo_t* info, void* ctx);
void  handler_sigint(int sig);
void  inicializar_timer_telemetria(timer_t* tid);
void  callback_boton(int gpio, int level, uint32_t tick);
void  inicializar_gpio(void);
void  liberar_gpio(void);

/* =========================================================
 * TAREA 1: EJECUTIVO CÍCLICO — CONTROL DE MOVIMIENTO DEL SERVO
 *
 * Corre en el hilo principal. Barrido suave 0° ↔ 180°.
 * Usa gpioServo() de pigpio para escribir el pulso PWM real.
 * Usa nanosleep() en lugar de sleep() para precisión.
 * ========================================================= */
void ejecutivo_ciclo_servo(void)
{
    int direccion = SERVO_INCREMENTO;
    struct timespec ts = {
        .tv_sec  = 0,
        .tv_nsec = SERVO_DELAY_US * 1000L   /* µs → ns */
    };

    printf("[SERVO] Ejecutivo cíclico iniciado. Barrido 0° ↔ 180°\n");

    while (atomic_load(&sistema_activo)) {

        if (atomic_load(&parada_emergencia)) {
            /* Servo detenido — pulso 0 desactiva la señal PWM en pigpio */
            gpioServo(GPIO_SERVO, 0);
            nanosleep(&ts, NULL);
            continue;
        }

        int pos_actual = atomic_load(&servo_posicion);
        int nueva_pos  = pos_actual + direccion;

        if (nueva_pos >= SERVO_MAX_GRADOS) {
            nueva_pos = SERVO_MAX_GRADOS;
            direccion = -SERVO_INCREMENTO;
        } else if (nueva_pos <= SERVO_MIN_GRADOS) {
            nueva_pos = SERVO_MIN_GRADOS;
            direccion = +SERVO_INCREMENTO;
        }

        atomic_store(&servo_posicion, nueva_pos);

        /* Escribir pulso PWM al servo físico */
        int pulso = GRADOS_A_PULSO(nueva_pos);
        gpioServo(GPIO_SERVO, pulso);

        nanosleep(&ts, NULL);
    }

    gpioServo(GPIO_SERVO, 0);
    printf("[SERVO] Ejecutivo cíclico detenido.\n");
}

/* =========================================================
 * CALLBACK DE PIGPIO — Flanco del botón de parada
 *
 * pigpio llama esta función en su propio hilo interno
 * cuando detecta un cambio de nivel en GPIO_BOTON.
 * Solo marca el evento; el hilo de seguridad lo procesa.
 * ========================================================= */
void callback_boton(int gpio, int level, uint32_t tick)
{
    (void)gpio;
    (void)tick;

    /* level == 0: botón presionado (lógica negada, pull-up activo) */
    if (level == 0) {
        int esperado = 0;
        atomic_compare_exchange_strong(&parada_emergencia, &esperado, 2);
    }
}

/* =========================================================
 * TAREA 2: HILO DE MONITOREO DE SEGURIDAD (POSIX Thread)
 *
 * Política SCHED_FIFO prioridad 80.
 * Detecta el estado 2 (evento del botón) y ejecuta la
 * respuesta de emergencia: escribe al LED físico vía pigpio
 * y mide la latencia de respuesta con CLOCK_MONOTONIC.
 * ========================================================= */
void* hilo_monitoreo_seguridad(void* arg)
{
    (void)arg;

    struct sched_param sp;
    sp.sched_priority = 80;

    int ret = pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);
    if (ret != 0) {
        fprintf(stderr,
            "[SEGURIDAD] ADVERTENCIA: No se pudo establecer SCHED_FIFO "
            "(errno=%d — %s).\n"
            "            Ejecute con 'sudo' para scheduling real-time.\n",
            ret, strerror(ret));
    } else {
        printf("[SEGURIDAD] Hilo iniciado — SCHED_FIFO prioridad 80.\n");
    }

    /* Poll cada 0.5 ms — balance entre latencia y carga de CPU */
    struct timespec ts_poll = { .tv_sec = 0, .tv_nsec = 500000L };

    while (atomic_load(&sistema_activo)) {

        if (atomic_load(&parada_emergencia) == 2) {

            struct timespec t0, t1;
            clock_gettime(CLOCK_MONOTONIC, &t0);

            /* Confirmar parada */
            atomic_store(&parada_emergencia, 1);

            /* Detener servo inmediatamente */
            gpioServo(GPIO_SERVO, 0);

            /* Encender LED de advertencia — escritura física GPIO */
            gpioWrite(GPIO_LED, PI_HIGH);

            clock_gettime(CLOCK_MONOTONIC, &t1);

            long latencia_ns = (t1.tv_sec  - t0.tv_sec)  * 1000000000L
                             + (t1.tv_nsec - t0.tv_nsec);

            printf("[ALERTA] Parada de emergencia activada - "
                   "Latencia detectada: %ld ns (%.3f ms)\n",
                   latencia_ns, latencia_ns / 1e6);
            fflush(stdout);
        }

        nanosleep(&ts_poll, NULL);
    }

    printf("[SEGURIDAD] Hilo de monitoreo detenido.\n");
    return NULL;
}

/* =========================================================
 * TAREA 3: HANDLER DE TELEMETRÍA (POSIX Timer, 1 Hz exacto)
 *
 * Disparado por SIGRTMIN cada 1 segundo exacto.
 * Informa posición del servo y modo del sistema.
 * No depende de sleep() ni del bucle principal.
 * ========================================================= */
void handler_telemetria(int sig, siginfo_t* info, void* ctx)
{
    (void)sig; (void)info; (void)ctx;

    int pos    = atomic_load(&servo_posicion);
    int alerta = atomic_load(&parada_emergencia);

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    struct tm* tm_info = localtime(&ts.tv_sec);
    char hora[16];
    strftime(hora, sizeof(hora), "%H:%M:%S", tm_info);

    printf("[TELEMETRIA %s.%03ld] Servo: %3d° | Modo: %-6s | LED: %s\n",
           hora,
           ts.tv_nsec / 1000000L,
           pos,
           alerta ? "ALERTA" : "Seguro",
           alerta ? "ON"     : "OFF");
    fflush(stdout);
}

void inicializar_timer_telemetria(timer_t* tid)
{
    struct sigevent  sev;
    struct sigaction sa;
    struct itimerspec its;

    memset(&sa, 0, sizeof(sa));
    sa.sa_flags     = SA_SIGINFO;
    sa.sa_sigaction = handler_telemetria;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGRTMIN, &sa, NULL) == -1) {
        perror("[TIMER] sigaction");
        exit(EXIT_FAILURE);
    }

    memset(&sev, 0, sizeof(sev));
    sev.sigev_notify          = SIGEV_SIGNAL;
    sev.sigev_signo           = SIGRTMIN;
    sev.sigev_value.sival_ptr = tid;

    if (timer_create(CLOCK_REALTIME, &sev, tid) == -1) {
        perror("[TIMER] timer_create");
        exit(EXIT_FAILURE);
    }

    its.it_value.tv_sec     = 1;  its.it_value.tv_nsec    = 0;
    its.it_interval.tv_sec  = 1;  its.it_interval.tv_nsec = 0;

    if (timer_settime(*tid, 0, &its, NULL) == -1) {
        perror("[TIMER] timer_settime");
        exit(EXIT_FAILURE);
    }

    printf("[TIMER] Timer de telemetría iniciado — disparo cada 1 segundo exacto.\n");
}

/* =========================================================
 * INICIALIZACIÓN DE GPIO CON PIGPIO
 * ========================================================= */
void inicializar_gpio(void)
{
    if (gpioInitialise() < 0) {
        fprintf(stderr,
            "[GPIO] Error al inicializar pigpio.\n"
            "       ¿Está ejecutando con sudo?\n"
            "       ¿Está corriendo en una Raspberry Pi?\n");
        exit(EXIT_FAILURE);
    }

    gpioSetMode(GPIO_SERVO, PI_OUTPUT);

    gpioSetMode(GPIO_BOTON, PI_INPUT);
    gpioSetPullUpDown(GPIO_BOTON, PI_PUD_UP);

    /*
     * Registrar callback en ambos flancos.
     * Dentro del callback filtramos level == 0 (presionado).
     * gpioSetAlertFunc es el mecanismo de pigpio para
     * notificaciones asíncronas de cambio de GPIO.
     */
    gpioSetAlertFunc(GPIO_BOTON, callback_boton);

    gpioSetMode(GPIO_LED, PI_OUTPUT);
    gpioWrite(GPIO_LED, PI_LOW);

    printf("[GPIO] pigpio inicializado correctamente.\n");
    printf("[GPIO]   Servo  → GPIO %d  (PWM, Pin 12)\n", GPIO_SERVO);
    printf("[GPIO]   Botón  → GPIO %d  (pull-up, Pin 13)\n", GPIO_BOTON);
    printf("[GPIO]   LED    → GPIO %d  (salida, Pin 11)\n\n", GPIO_LED);
}

/* =========================================================
 * LIBERACIÓN DE GPIO
 * ========================================================= */
void liberar_gpio(void)
{
    gpioServo(GPIO_SERVO, 0);
    gpioWrite(GPIO_LED, PI_LOW);
    gpioSetAlertFunc(GPIO_BOTON, NULL);  /* Desregistrar callback */
    gpioTerminate();
    printf("[GPIO] pigpio liberado.\n");
}

/* =========================================================
 * HANDLER SIGINT (Ctrl+C) — Apagado limpio
 * ========================================================= */
void handler_sigint(int sig)
{
    (void)sig;
    printf("\n[SISTEMA] Señal de apagado recibida. Deteniendo...\n");
    atomic_store(&sistema_activo, 0);
}

/* =========================================================
 * MAIN
 * ========================================================= */
int main(void)
{
    printf("╔══════════════════════════════════════════════════╗\n");
    printf("║   Sistema de Control de Barrera Crítica          ║\n");
    printf("║   Raspberry Pi 3 Model B+ — pigpio               ║\n");
    printf("║   Programación Concurrente — Ing. Storaccio      ║\n");
    printf("╚══════════════════════════════════════════════════╝\n\n");

    /* Inicializar pigpio y configurar pines */
    inicializar_gpio();

    /* Handlers de señal */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handler_sigint;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* Tarea 3: timer de telemetría 1 Hz */
    timer_t timer_telemetria;
    inicializar_timer_telemetria(&timer_telemetria);

    /* Tarea 2: hilo de monitoreo de seguridad */
    pthread_t hilo_seg;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);

    int ret = pthread_create(&hilo_seg, &attr, hilo_monitoreo_seguridad, NULL);
    if (ret != 0) {
        fprintf(stderr, "[MAIN] pthread_create: %s\n", strerror(ret));
        liberar_gpio();
        exit(EXIT_FAILURE);
    }
    pthread_attr_destroy(&attr);

    /* Tarea 1: ejecutivo cíclico del servo (hilo principal) */
    ejecutivo_ciclo_servo();

    /* Apagado limpio */
    struct itimerspec its_stop = { {0,0}, {0,0} };
    timer_settime(timer_telemetria, 0, &its_stop, NULL);
    timer_delete(timer_telemetria);

    pthread_join(hilo_seg, NULL);
    liberar_gpio();

    printf("[SISTEMA] Apagado completo. Posición final del servo: %d°\n",
           atomic_load(&servo_posicion));
    return EXIT_SUCCESS;
}