/*
 * actuador_server.c
 * Servidor multihilo de control de actuadores via Unix Domain Sockets
 *
 * Materia : Sistemas de Tiempo Real
 * Autor   : (alumno)
 *
 * ─── MODO PC (sin Raspberry Pi) ─────────────────────────────────────────────
 * Compilar con -DSIM_MODE (el Makefile lo hace automáticamente con `make pc`).
 * En este modo NO se usa pigpio: el "LED" se muestra en consola con colores.
 *
 * Para volver a la Raspberry con pigpio real, usar `make rpi` (requiere sudo).
 * ────────────────────────────────────────────────────────────────────────────
 *
 * Compilación PC  (automática):
 *   gcc -Wall -Wextra -std=c11 -pthread -DSIM_MODE actuador_server.c \
 *       -o actuador_server
 *
 * Compilación RPi (hardware real):
 *   gcc -Wall -Wextra -std=c11 -pthread actuador_server.c \
 *       -o actuador_server -lpigpio -lrt
 *
 * Uso:
 *   ./actuador_server
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>

/* ═══════════════════════════════════════════════════════════════════
 *  ABSTRACCIÓN DE HARDWARE
 *  Todas las llamadas al GPIO pasan por estas macros.
 *  Cambiar de PC ↔ RPi solo implica elegir el target en el Makefile.
 * ═══════════════════════════════════════════════════════════════════ */

#ifndef SIM_MODE
/* ── Modo Raspberry Pi real ─────────────────────────────────────── */
//#include <pigpio.h>
#define LED_GPIO     17
#define HW_INIT()    (gpioInitialise() >= 0)
#define HW_MODE()    gpioSetMode(LED_GPIO, PI_OUTPUT)
#define HW_WRITE(v)  gpioWrite(LED_GPIO, (v))
#define HW_TERM()    gpioTerminate()

#else
/* ── Modo simulación PC ─────────────────────────────────────────── */
/* pigpio es reemplazado completamente por funciones locales.        */
#define LED_GPIO     17   /* número de pin conservado por compatibilidad */
#define HW_INIT()    (sim_init(), 1)
#define HW_MODE()    ((void)0)
#define HW_WRITE(v)  sim_write(v)
#define HW_TERM()    sim_term()

/* Colores ANSI */
#define CLR_RESET  "\033[0m"
#define CLR_GREEN  "\033[1;32m"
#define CLR_RED    "\033[1;31m"
#define CLR_YELLOW "\033[1;33m"

static void sim_init(void)
{
    printf(CLR_YELLOW "[SIM] pigpio NO activo — modo simulación PC\n" CLR_RESET);
    printf(CLR_YELLOW "[SIM] GPIO BCM %d simulado en consola\n\n" CLR_RESET,
           LED_GPIO);
}

static void sim_write(int value)
{
    if (value)
        printf(CLR_GREEN  "[GPIO %d]  ■ LED ON \n" CLR_RESET, LED_GPIO);
    else
        printf(CLR_RED    "[GPIO %d]  □ LED OFF\n" CLR_RESET, LED_GPIO);
    fflush(stdout);
}

static void sim_term(void)
{
    printf(CLR_YELLOW "[SIM] Hardware simulado liberado.\n" CLR_RESET);
}

#endif /* SIM_MODE */

/* ═══════════════════════════════════════════════════════════════════
 *  CONSTANTES Y ESTADO GLOBAL
 * ═══════════════════════════════════════════════════════════════════ */

#define SOCK_PATH  "/tmp/control_led.sock"
#define BACKLOG    10
#define BUF_SIZE   64

typedef struct {
    int             encendido;   /* 0 = OFF, 1 = ON                  */
    pthread_mutex_t mutex;
} EstadoLED;

static EstadoLED led_state;
static int       server_fd = -1;   /* visible para el manejador de señal */

/* ═══════════════════════════════════════════════════════════════════
 *  MANEJADOR DE SEÑALES  (SIGINT / SIGTERM)
 * ═══════════════════════════════════════════════════════════════════ */

static void manejador_senal(int sig)
{
    (void)sig;
    printf("\n[servidor] Señal recibida — cerrando limpiamente...\n");

    if (server_fd != -1) {
        close(server_fd);
        server_fd = -1;
    }
    unlink(SOCK_PATH);

    HW_WRITE(0);   /* apagar LED (real o simulado) */
    HW_TERM();

    exit(EXIT_SUCCESS);
}

/* ═══════════════════════════════════════════════════════════════════
 *  HILO WORKER  — atiende a un cliente conectado
 * ═══════════════════════════════════════════════════════════════════ */

static void *worker_thread(void *arg)
{
    int client_fd = *((int *)arg);
    free(arg);

    char buf[BUF_SIZE];
    char resp[BUF_SIZE];

    printf("[worker %lu] Cliente conectado (fd=%d)\n",
           (unsigned long)pthread_self(), client_fd);

    /* Leer comando del cliente */
    ssize_t n = read(client_fd, buf, sizeof(buf) - 1);
    if (n <= 0) {
        if (n < 0) perror("[worker] read");
        close(client_fd);
        return NULL;
    }
    buf[n] = '\0';
    buf[strcspn(buf, "\r\n")] = '\0';   /* eliminar saltos de línea */

    printf("[worker %lu] Comando recibido: '%s'\n",
           (unsigned long)pthread_self(), buf);

    /* ── Sección crítica: mutex protege led_state y el GPIO ─── */
    pthread_mutex_lock(&led_state.mutex);

    if (strcmp(buf, "ON") == 0) {
        HW_WRITE(1);
        led_state.encendido = 1;
        snprintf(resp, sizeof(resp), "LED_OK: ON\n");

    } else if (strcmp(buf, "OFF") == 0) {
        HW_WRITE(0);
        led_state.encendido = 0;
        snprintf(resp, sizeof(resp), "LED_OK: OFF\n");

    } else if (strcmp(buf, "STATUS") == 0) {
        snprintf(resp, sizeof(resp), "LED_STATUS: %s\n",
                 led_state.encendido ? "ON" : "OFF");

    } else {
        snprintf(resp, sizeof(resp), "ERROR: cmd desconocido\n");
    }

    pthread_mutex_unlock(&led_state.mutex);
    /* ── Fin sección crítica ─────────────────────────────────── */

    /* Enviar respuesta */
    if (write(client_fd, resp, strlen(resp)) < 0)
        perror("[worker] write");

    printf("[worker %lu] Respuesta enviada: %s",
           (unsigned long)pthread_self(), resp);

    close(client_fd);
    return NULL;
}

/* ═══════════════════════════════════════════════════════════════════
 *  MAIN
 * ═══════════════════════════════════════════════════════════════════ */

int main(void)
{
#ifdef SIM_MODE
    printf("================================================\n");
    printf("  actuador_server  [MODO SIMULACIÓN — PC]       \n");
    printf("================================================\n\n");
#else
    printf("================================================\n");
    printf("  actuador_server  [MODO REAL — Raspberry Pi]   \n");
    printf("================================================\n\n");
#endif

    /* Inicializar hardware (real o simulado) */
    if (!HW_INIT()) {
        fprintf(stderr, "[servidor] Error al inicializar hardware.\n");
#ifndef SIM_MODE
        fprintf(stderr, "           ¿Estás corriendo como root (sudo)?\n");
#endif
        return EXIT_FAILURE;
    }
    HW_MODE();
    HW_WRITE(0);   /* estado inicial: LED apagado */

    /* Inicializar estado compartido y mutex */
    led_state.encendido = 0;
    if (pthread_mutex_init(&led_state.mutex, NULL) != 0) {
        perror("[servidor] pthread_mutex_init");
        HW_TERM();
        return EXIT_FAILURE;
    }

    /* Registrar manejadores de señal */
    signal(SIGINT,  manejador_senal);
    signal(SIGTERM, manejador_senal);

    /* Crear socket AF_UNIX SOCK_STREAM */
    server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("[servidor] socket");
        pthread_mutex_destroy(&led_state.mutex);
        HW_TERM();
        return EXIT_FAILURE;
    }

    unlink(SOCK_PATH);   /* eliminar socket anterior si existe */

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCK_PATH, sizeof(addr.sun_path) - 1);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("[servidor] bind");
        close(server_fd);
        pthread_mutex_destroy(&led_state.mutex);
        HW_TERM();
        return EXIT_FAILURE;
    }

    if (listen(server_fd, BACKLOG) < 0) {
        perror("[servidor] listen");
        close(server_fd);
        unlink(SOCK_PATH);
        pthread_mutex_destroy(&led_state.mutex);
        HW_TERM();
        return EXIT_FAILURE;
    }

    printf("[servidor] Socket: %s\n", SOCK_PATH);
    printf("[servidor] Esperando clientes... (Ctrl+C para salir)\n\n");

    /* ── Bucle principal: accept → pthread_create (hilo detached) ── */
    while (1) {
        int *client_fd = malloc(sizeof(int));
        if (!client_fd) { perror("[servidor] malloc"); continue; }

        *client_fd = accept(server_fd, NULL, NULL);
        if (*client_fd < 0) {
            if (errno == EINTR) { free(client_fd); break; }
            perror("[servidor] accept");
            free(client_fd);
            continue;
        }

        pthread_t      tid;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

        if (pthread_create(&tid, &attr, worker_thread, client_fd) != 0) {
            perror("[servidor] pthread_create");
            close(*client_fd);
            free(client_fd);
        }
        pthread_attr_destroy(&attr);
    }

    /* Limpieza al salir sin señal */
    close(server_fd);
    unlink(SOCK_PATH);
    pthread_mutex_destroy(&led_state.mutex);
    HW_WRITE(0);
    HW_TERM();
    printf("[servidor] Terminado.\n");
    return EXIT_SUCCESS;
}
