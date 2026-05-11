/*
 * actuador_client.c
 * Cliente de consola para el servidor de control de LED via UDS
 *
 * Materia : Sistemas de Tiempo Real
 * Autor   : (alumno)
 *
 * El cliente es idéntico para PC y Raspberry Pi.
 * No depende de pigpio.
 *
 * Compilación:
 *   gcc -Wall -Wextra -std=c11 actuador_client.c -o actuador_client
 *
 * Uso:
 *   ./actuador_client ON
 *   ./actuador_client OFF
 *   ./actuador_client STATUS
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

#define SOCK_PATH  "/tmp/control_led.sock"
#define BUF_SIZE   64

int main(int argc, char *argv[])
{
    /* ── Validar argumento ─────────────────────────────────────── */
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <ON|OFF|STATUS>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "ON")     != 0 &&
        strcmp(cmd, "OFF")    != 0 &&
        strcmp(cmd, "STATUS") != 0) {
        fprintf(stderr, "Comando inválido: '%s'\n"
                        "Comandos válidos: ON  OFF  STATUS\n", cmd);
        return EXIT_FAILURE;
    }

    /* ── Crear socket AF_UNIX ──────────────────────────────────── */
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCK_PATH, sizeof(addr.sun_path) - 1);

    /* ── Conectar al servidor ──────────────────────────────────── */
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        fprintf(stderr, "  ¿Está corriendo el servidor (./actuador_server)?\n");
        close(fd);
        return EXIT_FAILURE;
    }

    /* ── Enviar comando ────────────────────────────────────────── */
    if (write(fd, cmd, strlen(cmd)) < 0) {
        perror("write");
        close(fd);
        return EXIT_FAILURE;
    }

    /* Indicar fin de escritura (permite al servidor hacer read() sin bloqueo) */
    shutdown(fd, SHUT_WR);

    /* ── Recibir respuesta ─────────────────────────────────────── */
    char buf[BUF_SIZE];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    if (n < 0) {
        perror("read");
        close(fd);
        return EXIT_FAILURE;
    }
    buf[n] = '\0';

    printf("Respuesta del servidor: %s", buf);
    if (n > 0 && buf[n - 1] != '\n') printf("\n");

    close(fd);
    return EXIT_SUCCESS;
}
