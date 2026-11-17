#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>

#define SOCKET_PATH "./socket"

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
    int sock_fd;
    struct sockaddr_un addr;

    signal(SIGPIPE, SIG_IGN);

    sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock_fd == -1) {
        perror("socket");
        return EXIT_FAILURE;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (connect(sock_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("connect");
        close(sock_fd);
        return EXIT_FAILURE;
    }

    printf("Подключено к серверу. Отправка сообщений...\n");
    fflush(stdout);

    const char *messages[] = {
        "Hello World!\n",
        "This is a Test Message!!!\n",
        "Unix domain sockets are awesome.\n",
    };
    const size_t count = sizeof(messages) / sizeof(messages[0]);

    for (size_t i = 0; i < count; i++) {
        printf("[CLIENT] Отправляю: %s", messages[i]);
        fflush(stdout);

        if (write_all(sock_fd, messages[i], strlen(messages[i])) == -1) {
            perror("write");
            close(sock_fd);
            return EXIT_FAILURE;
        }
    }

    printf("[CLIENT] Все сообщения отправлены. Закрытие соединения...\n");
    close(sock_fd);   /* разрыв соединения → сервер получит EOF */
    return EXIT_SUCCESS;
}