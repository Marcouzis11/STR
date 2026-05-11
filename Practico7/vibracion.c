/*
 * medidor_vibracion.c
 * Procesamiento Concurrente de Señales Inerciales
 * Materia: Sistemas de Tiempo Real
 * Adaptado para: Raspberry Pi 5 + Sensor SW-420 + librería lgpio
 *
 * El sensor SW-420 es digital (HIGH=sin vibración, LOW=vibración detectada).
 * Se genera una "señal" de 3 ejes simulando energía de vibración:
 *   - Eje X: amplitud basada en frecuencia de eventos recientes
 *   - Eje Y: variante de fase respecto a X
 *   - Eje Z: componente de baja frecuencia
 *
 * Compilación:
 *   gcc -o medidor_vibracion medidor_vibracion.c -llgpio -lpthread -lrt -lm
 *
 * Uso:
 *   sudo ./medidor_vibracion | python3 plotter_3ejes.py
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <mqueue.h>
#include <signal.h>
#include <math.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <lgpio.h>

/* ─── Configuración de hardware ─────────────────────────────────────── */
#define GPIO_SW420      17      /* Pin BCM conectado a la salida DO del SW-420 */
#define SAMPLE_RATE_HZ  100     /* Frecuencia de muestreo: 100 Hz = cada 10 ms  */
#define SAMPLE_MS       10      /* Período en milisegundos                       */

/* ─── Configuración de la cola POSIX ────────────────────────────────── */
#define QUEUE_NAME      "/vibration_queue"
#define QUEUE_MAX_MSG   50
#define QUEUE_PRIORITY  0

/* ─── Filtro de media móvil ─────────────────────────────────────────── */
#define FILTER_N        10      /* Número de muestras para el promedio móvil */

/* ─── Estructura de datos entre hilos ───────────────────────────────── */
typedef struct {
    float x;
    float y;
    float z;
    struct timespec ts;         /* Timestamp de adquisición */
} imu_sample_t;

/* ─── Variables globales ─────────────────────────────────────────────── */
static volatile int running = 1;
static int gpio_handle = -1;
static mqd_t mq_fd = (mqd_t)-1;

/* Mutex + variables compartidas (para hilo opcional de actuación) */
static pthread_mutex_t mutex_estado = PTHREAD_MUTEX_INITIALIZER;
static float estado_x_filtrado = 0.0f;
static float estado_y_filtrado = 0.0f;
static float estado_z_filtrado = 0.0f;

/* ─── Utilidades de tiempo ───────────────────────────────────────────── */
static double timespec_to_sec(const struct timespec *ts) {
    return (double)ts->tv_sec + (double)ts->tv_nsec * 1e-9;
}

static void sleep_ms(int ms) {
    struct timespec ts = { .tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

/* ─── Manejador de señales ───────────────────────────────────────────── */
static void handle_signal(int sig) {
    (void)sig;
    running = 0;
}

/* ═══════════════════════════════════════════════════════════════════════
 * HILO PRODUCTOR: Adquisición del SW-420 a 100 Hz
 * ═══════════════════════════════════════════════════════════════════════ */
static void *hilo_productor(void *arg) {
    (void)arg;

    /*
     * El SW-420 entrega señal DIGITAL:
     *   - GPIO HIGH (1) → Sin vibración
     *   - GPIO LOW  (0) → Vibración detectada
     *
     * Estrategia para generar señal "análoga" de 3 ejes:
     * Mantenemos una ventana de tiempo de 1 segundo. Contamos cuántos
     * eventos de vibración ocurrieron en esa ventana y construimos
     * amplitudes con diferente fase para X, Y, Z.
     */

    /* Ventana circular de timestamps de eventos (últimos 1 segundo) */
    #define EVENT_WINDOW    100     /* máx eventos a recordar */
    double event_times[EVENT_WINDOW];
    int event_head = 0;
    int event_count = 0;

    int prev_state = 1;             /* estado anterior del pin */
    double t_start = 0.0;
    struct timespec ts_now;

    clock_gettime(CLOCK_MONOTONIC, &ts_now);
    t_start = timespec_to_sec(&ts_now);

    fprintf(stderr, "[Productor] Iniciado. GPIO %d, %d Hz\n", GPIO_SW420, SAMPLE_RATE_HZ);

    while (running) {
        struct timespec t_begin;
        clock_gettime(CLOCK_MONOTONIC, &t_begin);
        double t_now = timespec_to_sec(&t_begin) - t_start;

        /* 1. Leer GPIO */
        int gpio_val = lgGpioRead(gpio_handle, GPIO_SW420);
        if (gpio_val < 0) {
            fprintf(stderr, "[Productor] Error leyendo GPIO: %d\n", gpio_val);
        }

        /* 2. Detectar flanco descendente (HIGH→LOW = inicio de vibración) */
        if (prev_state == 1 && gpio_val == 0) {
            event_times[event_head % EVENT_WINDOW] = t_now;
            event_head++;
            if (event_count < EVENT_WINDOW) event_count++;
        }
        prev_state = gpio_val;

        /* 3. Limpiar eventos fuera de la ventana de 1 segundo */
        int valid = 0;
        for (int i = 0; i < event_count && i < EVENT_WINDOW; i++) {
            int idx = (event_head - 1 - i + EVENT_WINDOW * 2) % EVENT_WINDOW;
            if ((t_now - event_times[idx]) <= 1.0) valid++;
        }

        /*
         * 4. Construir valores de 3 ejes a partir de:
         *    - "valid" = número de flancos en el último segundo (frecuencia Hz)
         *    - Amplitud normalizada: clampear entre 0 y ~50 Hz de eventos
         *    - Fase diferente por eje para simular distintos planos
         *    - Si GPIO está bajo (vibrando ahora), amplificar la señal
         */
        float amp = (float)valid / 20.0f;   /* 0.0 – ~5.0 g equivalente */
        if (amp > 5.0f) amp = 5.0f;

        /* Si hay vibración activa en este momento, agregar componente instantánea */
        float burst = (gpio_val == 0) ? 1.0f : 0.0f;

        float x = amp * (float)sin(2.0 * M_PI * 5.0 * t_now) + burst * 2.0f;
        float y = amp * (float)sin(2.0 * M_PI * 5.0 * t_now + M_PI / 3.0) + burst * 1.5f;
        float z = amp * (float)sin(2.0 * M_PI * 2.5 * t_now) + burst * 1.0f + 1.0f; /* +1g gravedad */

        /* 5. Empaquetar y enviar a la cola */
        imu_sample_t sample = { .x = x, .y = y, .z = z, .ts = t_begin };

        if (mq_send(mq_fd, (const char *)&sample, sizeof(sample), QUEUE_PRIORITY) < 0) {
            if (errno == EAGAIN) {
                fprintf(stderr, "[Productor] Cola llena, muestra descartada\n");
            } else {
                perror("[Productor] mq_send");
            }
        }

        /* 6. Dormir el tiempo restante del período (10 ms) */
        struct timespec t_end;
        clock_gettime(CLOCK_MONOTONIC, &t_end);
        double elapsed_ms = (timespec_to_sec(&t_end) - timespec_to_sec(&t_begin)) * 1000.0;
        int sleep_time = SAMPLE_MS - (int)elapsed_ms;
        if (sleep_time > 0) sleep_ms(sleep_time);
    }

    fprintf(stderr, "[Productor] Finalizado.\n");
    return NULL;
}

/* ═══════════════════════════════════════════════════════════════════════
 * HILO CONSUMIDOR: Filtrado y despacho CSV por stdout
 * ═══════════════════════════════════════════════════════════════════════ */
static void *hilo_consumidor(void *arg) {
    (void)arg;

    /* Buffers para filtro de media móvil (N muestras por eje) */
    float buf_x[FILTER_N] = {0};
    float buf_y[FILTER_N] = {0};
    float buf_z[FILTER_N] = {0};
    int   buf_idx = 0;
    int   buf_fill = 0;         /* cuántas muestras válidas hay en el buffer */

    imu_sample_t sample;
    ssize_t bytes;

    fprintf(stderr, "[Consumidor] Iniciado. Filtro media móvil N=%d\n", FILTER_N);

    while (running) {
        /* Recibir muestra de la cola (bloqueante con timeout) */
        struct timespec timeout;
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_nsec += 50000000L;   /* 50 ms de timeout */
        if (timeout.tv_nsec >= 1000000000L) {
            timeout.tv_sec++;
            timeout.tv_nsec -= 1000000000L;
        }

        bytes = mq_timedreceive(mq_fd, (char *)&sample, sizeof(sample), NULL, &timeout);
        if (bytes < 0) {
            if (errno == ETIMEDOUT || errno == EINTR) continue;
            perror("[Consumidor] mq_timedreceive");
            continue;
        }

        /* Actualizar buffers circulares */
        buf_x[buf_idx] = sample.x;
        buf_y[buf_idx] = sample.y;
        buf_z[buf_idx] = sample.z;
        buf_idx = (buf_idx + 1) % FILTER_N;
        if (buf_fill < FILTER_N) buf_fill++;

        /* Calcular media móvil */
        float sum_x = 0, sum_y = 0, sum_z = 0;
        for (int i = 0; i < buf_fill; i++) {
            sum_x += buf_x[i];
            sum_y += buf_y[i];
            sum_z += buf_z[i];
        }
        float fx = sum_x / buf_fill;
        float fy = sum_y / buf_fill;
        float fz = sum_z / buf_fill;

        /* Actualizar variables compartidas (para hilo de actuación opcional) */
        pthread_mutex_lock(&mutex_estado);
        estado_x_filtrado = fx;
        estado_y_filtrado = fy;
        estado_z_filtrado = fz;
        pthread_mutex_unlock(&mutex_estado);

        /* Enviar CSV a stdout — SOLO datos, errores van a stderr */
        printf("%.4f,%.4f,%.4f\n", fx, fy, fz);
        fflush(stdout);
    }

    fprintf(stderr, "[Consumidor] Finalizado.\n");
    return NULL;
}

/* ═══════════════════════════════════════════════════════════════════════
 * HILO OPCIONAL: Actuación (simula PWM de servo sin hardware SG90)
 * Reemplazá lgGpioServo() por la llamada real si tenés el servo conectado
 * ═══════════════════════════════════════════════════════════════════════ */
#define GPIO_SERVO      18      /* Pin BCM para PWM del servo */
#define ENABLE_SERVO    0       /* Cambiar a 1 para activar el hilo */

#if ENABLE_SERVO
static void *hilo_servo(void *arg) {
    (void)arg;
    fprintf(stderr, "[Servo] Hilo iniciado en GPIO %d\n", GPIO_SERVO);

    while (running) {
        float angulo;

        pthread_mutex_lock(&mutex_estado);
        angulo = estado_x_filtrado;
        pthread_mutex_unlock(&mutex_estado);

        /*
         * Mapear el valor filtrado del eje X a microsegundos de pulso PWM:
         * Rango típico SW-420: -5.0 a +5.0 → 500 µs a 2500 µs (servo SG90)
         */
        int pw_us = (int)(1500.0f + angulo * 200.0f);
        if (pw_us < 500)  pw_us = 500;
        if (pw_us > 2500) pw_us = 2500;

        /* lgGpioServo(handle, pin, pulso_µs) */
        int ret = lgGpioServo(gpio_handle, GPIO_SERVO, pw_us);
        if (ret < 0) {
            fprintf(stderr, "[Servo] Error PWM: %d\n", ret);
        }

        sleep_ms(20);   /* Servo típico: actualizar cada 20 ms */
    }

    /* Apagar servo al salir */
    lgGpioServo(gpio_handle, GPIO_SERVO, 0);
    fprintf(stderr, "[Servo] Finalizado.\n");
    return NULL;
}
#endif  /* ENABLE_SERVO */

/* ═══════════════════════════════════════════════════════════════════════
 * MAIN
 * ═══════════════════════════════════════════════════════════════════════ */
int main(void) {
    signal(SIGINT,  handle_signal);
    signal(SIGTERM, handle_signal);

    /* ── 1. Inicializar lgpio ──────────────────────────────────────── */
    gpio_handle = lgGpiochipOpen(0);    /* /dev/gpiochip0 en RPi 5 */
    if (gpio_handle < 0) {
        fprintf(stderr, "[Main] Error abriendo gpiochip0: %d\n", gpio_handle);
        return EXIT_FAILURE;
    }

    /* Configurar GPIO del SW-420 como entrada con pull-up interno */
    int ret = lgGpioClaimInput(gpio_handle, LG_SET_PULL_UP, GPIO_SW420);
    if (ret < 0) {
        fprintf(stderr, "[Main] Error configurando GPIO %d: %d\n", GPIO_SW420, ret);
        lgGpiochipClose(gpio_handle);
        return EXIT_FAILURE;
    }
    fprintf(stderr, "[Main] GPIO %d configurado (SW-420).\n", GPIO_SW420);

#if ENABLE_SERVO
    ret = lgGpioClaimOutput(gpio_handle, 0, GPIO_SERVO, 0);
    if (ret < 0) {
        fprintf(stderr, "[Main] Error configurando GPIO servo %d: %d\n", GPIO_SERVO, ret);
    }
#endif

    /* ── 2. Crear cola de mensajes POSIX ──────────────────────────── */
    mq_unlink(QUEUE_NAME);  /* Limpiar cola anterior si existía */

    struct mq_attr attr = {
        .mq_flags   = 0,
        .mq_maxmsg  = QUEUE_MAX_MSG,
        .mq_msgsize = sizeof(imu_sample_t),
        .mq_curmsgs = 0
    };

    mq_fd = mq_open(QUEUE_NAME, O_CREAT | O_RDWR, 0644, &attr);
    if (mq_fd == (mqd_t)-1) {
        perror("[Main] mq_open");
        lgGpiochipClose(gpio_handle);
        return EXIT_FAILURE;
    }
    fprintf(stderr, "[Main] Cola '%s' creada (max %d msgs, %zu bytes/msg).\n",
            QUEUE_NAME, QUEUE_MAX_MSG, sizeof(imu_sample_t));

    /* ── 3. Lanzar hilos ──────────────────────────────────────────── */
    pthread_t th_prod, th_cons;
#if ENABLE_SERVO
    pthread_t th_servo;
#endif

    if (pthread_create(&th_prod, NULL, hilo_productor, NULL) != 0) {
        perror("[Main] pthread_create productor");
        goto cleanup;
    }
    if (pthread_create(&th_cons, NULL, hilo_consumidor, NULL) != 0) {
        perror("[Main] pthread_create consumidor");
        goto cleanup;
    }
#if ENABLE_SERVO
    if (pthread_create(&th_servo, NULL, hilo_servo, NULL) != 0) {
        perror("[Main] pthread_create servo");
    }
#endif

    fprintf(stderr, "[Main] Sistema iniciado. Sensor listo.\n");
    fprintf(stderr, "[Main] Presioná Ctrl+C para detener.\n");

    /* ── 4. Esperar a que los hilos terminen ──────────────────────── */
    pthread_join(th_prod, NULL);
    pthread_join(th_cons, NULL);
#if ENABLE_SERVO
    pthread_join(th_servo, NULL);
#endif

cleanup:
    /* ── 5. Limpieza ──────────────────────────────────────────────── */
    if (mq_fd != (mqd_t)-1) {
        mq_close(mq_fd);
        mq_unlink(QUEUE_NAME);
    }
    if (gpio_handle >= 0) {
        lgGpioFree(gpio_handle, GPIO_SW420);
        lgGpiochipClose(gpio_handle);
    }
    pthread_mutex_destroy(&mutex_estado);

    fprintf(stderr, "[Main] Recursos liberados. Fin.\n");
    return EXIT_SUCCESS;
}