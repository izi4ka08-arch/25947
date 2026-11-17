#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>

#define SOCKET_PATH "./socket"
#define BUF_SIZE    4096
#define BACKLOG     5

static void die(const char *msg)
{
    perror(msg);
    unlink(SOCKET_PATH);
    exit(EXIT_FAILURE);
}

static int write_all(int fd, const char *buf, size_t len)
{
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(fd, buf + off, len - off);
        if (n == -1) {
            if (errno == EINTR) continue;
            return -1;
        }
        off += (size_t)n;
    }
    return 0;
}

int main(void)
{
    int server_fd, client_fd;
    struct sockaddr_un addr;
    char buf[BUF_SIZE];

    /* Чтобы write в закрытый сокет не убивал процесс */
    signal(SIGPIPE, SIG_IGN);

    /* Удаляем старый файл сокета, иначе bind() упадёт с EADDRINUSE */
    if (unlink(SOCKET_PATH) == -1 && errno != ENOENT) {
        perror("unlink");
        return EXIT_FAILURE;
    }

    server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd == -1) die("socket");

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        close(server_fd);
        die("bind");
    }

    if (listen(server_fd, BACKLOG) == -1) {
        close(server_fd);
        die("listen");
    }

    printf("Сервер запущен. Ожидание подключений на %s...\n", SOCKET_PATH);
    fflush(stdout);

    client_fd = accept(server_fd, NULL, NULL);
    if (client_fd == -1) {
        close(server_fd);
        die("accept");
    }

    printf("Клиент подключился!\n");
    fflush(stdout);

    ssize_t n;
    while ((n = read(client_fd, buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < n; i++) {
            buf[i] = (char)toupper((unsigned char)buf[i]);
        }
        if (write_all(STDOUT_FILENO, buf, (size_t)n) == -1) {
            perror("write");
            break;
        }
    }

    if (n == -1 && errno != EINTR) {
        perror("read");
    }

    close(client_fd);
    close(server_fd);
    unlink(SOCKET_PATH);

    printf("\nКлиент отключился. Завершение работы сервера.\n");
    return EXIT_SUCCESS;
}