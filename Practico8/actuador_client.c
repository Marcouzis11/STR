/*
 * actuador_client.c
 * Cliente de consola para el servidor de control de LED via UDS
 *
 * Materia : Sistemas de Tiempo Real
 * Autor   : (alumno)
 * Fecha   : 2026
 *
 * Compilacion:
 *   gcc -Wall -Wextra actuador_client.c -o actuador_client
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

/* ─── Constantes ────────────────────────────────────────────────── */
#define SOCK_PATH   "/tmp/control_led.sock"
#define BUF_SIZE    64

/* ─── main ──────────────────────────────────────────────────────── */
int main(int argc, char *argv[])
{
    /* Validar argumentos */
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <ON|OFF|STATUS>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *cmd = argv[1];

    /* Validar comando permitido */
    if (strcmp(cmd, "ON")     != 0 &&
        strcmp(cmd, "OFF")    != 0 &&
        strcmp(cmd, "STATUS") != 0) {
        fprintf(stderr, "Comando inválido: '%s'. Use ON, OFF o STATUS.\n", cmd);
        return EXIT_FAILURE;
    }

    /* Crear socket AF_UNIX SOCK_STREAM */
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }

    /* Dirección del servidor */
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCK_PATH, sizeof(addr.sun_path) - 1);

    /* Conectar al servidor */
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect (¿el servidor está corriendo?)");
        close(fd);
        return EXIT_FAILURE;
    }

    /* Enviar el comando (sin '\n' para mayor robustez,
       el servidor acepta ambas formas)                             */
    ssize_t sent = write(fd, cmd, strlen(cmd));
    if (sent < 0) {
        perror("write");
        close(fd);
        return EXIT_FAILURE;
    }

    /* Indicar fin de escritura: el servidor puede hacer un read
       bloqueante; shutdown(SHUT_WR) es una buena práctica.        */
    shutdown(fd, SHUT_WR);

    /* Leer la respuesta del servidor */
    char buf[BUF_SIZE];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    if (n < 0) {
        perror("read");
        close(fd);
        return EXIT_FAILURE;
    }
    buf[n] = '\0';

    /* Mostrar resultado al usuario */
    printf("Respuesta del servidor: %s", buf);
    if (buf[n - 1] != '\n')
        printf("\n");

    close(fd);
    return EXIT_SUCCESS;
}
