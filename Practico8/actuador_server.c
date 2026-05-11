/*
 * actuador_server.c
 * Servidor multihilo de control de actuadores via Unix Domain Sockets
 *
 * Materia : Sistemas de Tiempo Real
 * Autor   : (alumno)
 * Fecha   : 2026
 *
 * Compilacion:
 *   gcc -Wall -Wextra -pthread actuador_server.c -o actuador_server -lpigpio -lrt
 *
 * Uso:
 *   sudo ./actuador_server
 *
 * Nota: pigpio requiere privilegios de root (sudo).
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
#include <pigpio.h>

/* ─── Constantes ────────────────────────────────────────────────── */
#define SOCK_PATH   "/tmp/control_led.sock"
#define LED_GPIO    17          /* GPIO BCM del LED                  */
#define BACKLOG     10          /* Clientes en cola de listen()      */
#define BUF_SIZE    64          /* Tamaño del buffer de comandos      */

/* ─── Estado global compartido ──────────────────────────────────── */
typedef struct {
    int            encendido;   /* 0 = OFF, 1 = ON                   */
    pthread_mutex_t mutex;
} EstadoLED;

static EstadoLED led_state;

/* Descriptor del socket servidor (para poder cerrarlo en la señal) */
static int server_fd = -1;

/* ─── Limpieza al recibir SIGINT / SIGTERM ──────────────────────── */
static void manejador_senal(int sig)
{
    (void)sig;
    printf("\n[servidor] Señal recibida, cerrando...\n");

    if (server_fd != -1) {
        close(server_fd);
        server_fd = -1;
    }
    unlink(SOCK_PATH);          /* Eliminar el fichero de socket     */

    /* Apagar LED y liberar pigpio */
    gpioWrite(LED_GPIO, 0);
    gpioTerminate();

    exit(EXIT_SUCCESS);
}

/* ─── Hilo worker: atiende a un cliente conectado ──────────────── */
static void *worker_thread(void *arg)
{
    int client_fd = *((int *)arg);
    free(arg);                  /* Liberar memoria del descriptor    */

    char buf[BUF_SIZE];
    char resp[BUF_SIZE];

    printf("[worker %lu] Cliente conectado (fd=%d)\n",
           (unsigned long)pthread_self(), client_fd);

    /* Leer el comando (puede venir en uno o varios segmentos,
       pero para comandos cortos un único read es suficiente)       */
    ssize_t n = read(client_fd, buf, sizeof(buf) - 1);
    if (n <= 0) {
        if (n < 0)
            perror("[worker] read");
        goto fin;
    }
    buf[n] = '\0';

    /* Eliminar posibles saltos de línea enviados por el cliente     */
    buf[strcspn(buf, "\r\n")] = '\0';

    printf("[worker %lu] Comando recibido: '%s'\n",
           (unsigned long)pthread_self(), buf);

    /* ── Sección crítica: mutex protege led_state ─────────────── */
    pthread_mutex_lock(&led_state.mutex);

    if (strcmp(buf, "ON") == 0) {
        gpioWrite(LED_GPIO, 1);
        led_state.encendido = 1;
        snprintf(resp, sizeof(resp), "LED_OK: ON\n");

    } else if (strcmp(buf, "OFF") == 0) {
        gpioWrite(LED_GPIO, 0);
        led_state.encendido = 0;
        snprintf(resp, sizeof(resp), "LED_OK: OFF\n");

    } else if (strcmp(buf, "STATUS") == 0) {
        snprintf(resp, sizeof(resp), "LED_STATUS: %s\n",
                 led_state.encendido ? "ON" : "OFF");

    } else {
        snprintf(resp, sizeof(resp), "ERROR: Comando desconocido '%s'\n", buf);
    }

    pthread_mutex_unlock(&led_state.mutex);
    /* ── Fin sección crítica ─────────────────────────────────── */

    /* Enviar respuesta al cliente */
    if (write(client_fd, resp, strlen(resp)) < 0)
        perror("[worker] write");

    printf("[worker %lu] Respuesta enviada: %s",
           (unsigned long)pthread_self(), resp);

fin:
    close(client_fd);
    printf("[worker %lu] Conexión cerrada.\n",
           (unsigned long)pthread_self());
    return NULL;
}

/* ─── main ──────────────────────────────────────────────────────── */
int main(void)
{
    /* Inicializar pigpio */
    if (gpioInitialise() < 0) {
        fprintf(stderr, "[servidor] Error al inicializar pigpio. "
                        "¿Está corriendo como root?\n");
        return EXIT_FAILURE;
    }
    gpioSetMode(LED_GPIO, PI_OUTPUT);
    gpioWrite(LED_GPIO, 0);     /* LED apagado al inicio             */
    printf("[servidor] pigpio inicializado. GPIO %d configurado como salida.\n",
           LED_GPIO);

    /* Inicializar estado y mutex */
    led_state.encendido = 0;
    if (pthread_mutex_init(&led_state.mutex, NULL) != 0) {
        perror("[servidor] pthread_mutex_init");
        gpioTerminate();
        return EXIT_FAILURE;
    }

    /* Registrar manejadores de señal */
    signal(SIGINT,  manejador_senal);
    signal(SIGTERM, manejador_senal);

    /* Crear socket AF_UNIX de tipo SOCK_STREAM */
    server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("[servidor] socket");
        pthread_mutex_destroy(&led_state.mutex);
        gpioTerminate();
        return EXIT_FAILURE;
    }

    /* Eliminar socket anterior si existe */
    unlink(SOCK_PATH);

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCK_PATH, sizeof(addr.sun_path) - 1);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("[servidor] bind");
        close(server_fd);
        pthread_mutex_destroy(&led_state.mutex);
        gpioTerminate();
        return EXIT_FAILURE;
    }

    if (listen(server_fd, BACKLOG) < 0) {
        perror("[servidor] listen");
        close(server_fd);
        unlink(SOCK_PATH);
        pthread_mutex_destroy(&led_state.mutex);
        gpioTerminate();
        return EXIT_FAILURE;
    }

    printf("[servidor] Escuchando en %s ...\n", SOCK_PATH);
    printf("[servidor] Esperando clientes (Ctrl+C para salir).\n");

    /* ── Bucle principal de aceptación ─────────────────────────── */
    while (1) {
        int *client_fd = malloc(sizeof(int));
        if (!client_fd) {
            perror("[servidor] malloc");
            continue;
        }

        *client_fd = accept(server_fd, NULL, NULL);
        if (*client_fd < 0) {
            /* accept() interrumpido por señal → salir              */
            if (errno == EINTR) {
                free(client_fd);
                break;
            }
            perror("[servidor] accept");
            free(client_fd);
            continue;
        }

        /* Crear hilo detached (se limpia solo al terminar)         */
        pthread_t tid;
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

    /* Limpieza final (también alcanzable sin señal) */
    close(server_fd);
    unlink(SOCK_PATH);
    pthread_mutex_destroy(&led_state.mutex);
    gpioWrite(LED_GPIO, 0);
    gpioTerminate();
    printf("[servidor] Terminado limpiamente.\n");
    return EXIT_SUCCESS;
}
