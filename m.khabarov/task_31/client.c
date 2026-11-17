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

int main(int argc, char *argv[])
{
    const char *name = (argc > 1) ? argv[1] : "CLIENT";

    signal(SIGPIPE, SIG_IGN);

    int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock_fd == -1) {
        perror("socket");
        return EXIT_FAILURE;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (connect(sock_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("connect");
        close(sock_fd);
        return EXIT_FAILURE;
    }

    printf("[%s] Подключён к серверу. Отправляю сообщения...\n", name);
    fflush(stdout);

    const char *texts[] = {
        "Hello from client!",
        "This is a test message.",
        "Lowercase and UPPERCASE mixed.",
    };
    const int count = (int)(sizeof(texts) / sizeof(texts[0]));

    for (int i = 0; i < count; i++) {
        char line[256];
        int len = snprintf(line, sizeof(line), "[%s] %s\n", name, texts[i]);
        if (len < 0) continue;

        printf("[%s] Отправляю: %s", name, line);
        fflush(stdout);

        if (write_all(sock_fd, line, (size_t)len) == -1) {
            perror("write");
            close(sock_fd);
            return EXIT_FAILURE;
        }

        /* Пауза, чтобы продемонстрировать параллельную обработку */
        usleep(300000);   /* 0.3 с */
    }

    printf("[%s] Все сообщения отправлены. Закрытие соединения...\n", name);
    close(sock_fd);
    return EXIT_SUCCESS;
}