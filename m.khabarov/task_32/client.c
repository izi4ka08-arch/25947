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

int main(int argc, char *argv[]) {
    const char *name = (argc > 1) ? argv[1] : "client";
    signal(SIGPIPE, SIG_IGN);

    int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock_fd == -1) { perror("socket"); return EXIT_FAILURE; }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (connect(sock_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("connect");
        close(sock_fd);
        return EXIT_FAILURE;
    }

    const char *texts[] = {
        "hello from client\n",
        "this is a test message\n",
        "asynchronous io is awesome\n",
    };
    int count = (int)(sizeof(texts) / sizeof(texts[0]));

    printf("[%s] Подключён. Отправляю %d сообщений...\n", name, count);
    fflush(stdout);

    for (int i = 0; i < count; i++) {
        char line[256];
        int len = snprintf(line, sizeof(line), "[%s] %s", name, texts[i]);
        if (len < 0) continue;
        printf("[%s] Отправляю: %s", name, line);
        fflush(stdout);

        size_t off = 0;
        while (off < (size_t)len) {
            ssize_t w = write(sock_fd, line + off, (size_t)len - off);
            if (w == -1) {
                if (errno == EINTR) continue;
                perror("write");
                close(sock_fd);
                return EXIT_FAILURE;
            }
            off += (size_t)w;
        }
        usleep(500000);   /* 0.5 с — чтобы было видно чередование */
    }

    printf("[%s] Готово. Закрываю соединение.\n", name);
    close(sock_fd);
    return EXIT_SUCCESS;
}