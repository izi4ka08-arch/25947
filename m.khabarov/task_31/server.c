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
#include <poll.h>

#define SOCKET_PATH  "./socket"
#define BUF_SIZE     4096
#define BACKLOG      10
#define MAX_CLIENTS  64

static volatile sig_atomic_t stop_requested = 0;

static void on_sigint(int sig)
{
    (void)sig;
    stop_requested = 1;
}

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

/* Считаем количество активных клиентов (без слушающего сокета) */
static int count_clients(struct pollfd *fds, nfds_t nfds)
{
    int c = 0;
    for (nfds_t i = 1; i < nfds; i++) {
        if (fds[i].fd != -1) c++;
    }
    return c;
}

/* Находим свободный слот в массиве fds, возвращаем индекс или -1 */
static int find_free_slot(struct pollfd *fds, nfds_t *nfds)
{
    for (nfds_t i = 1; i < *nfds; i++) {
        if (fds[i].fd == -1) return (int)i;
    }
    if (*nfds < MAX_CLIENTS + 1) {
        return (int)(*nfds)++;
    }
    return -1;
}

int main(void)
{
    int server_fd;
    struct sockaddr_un addr;

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;   /* без SA_RESTART: poll должен прерываться */
    sigaction(SIGINT, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

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

    printf("[СЕРВЕР] Запущен. Ожидание подключений на %s...\n", SOCKET_PATH);
    printf("[СЕРВЕР] Нажмите Ctrl+C для остановки.\n");
    fflush(stdout);

    struct pollfd fds[MAX_CLIENTS + 1];
    for (int i = 0; i < MAX_CLIENTS + 1; i++) {
        fds[i].fd = -1;
        fds[i].events = 0;
        fds[i].revents = 0;
    }
    fds[0].fd = server_fd;
    fds[0].events = POLLIN;
    nfds_t nfds = 1;

    char buf[BUF_SIZE];

    while (!stop_requested) {
        int ready = poll(fds, nfds, 1000);
        if (ready == -1) {
            if (errno == EINTR) continue;   /* Ctrl+C или другой сигнал */
            perror("poll");
            break;
        }
        if (ready == 0) continue;           /* таймаут — просто проверяем флаг */

        for (nfds_t i = 0; i < nfds; i++) {
            if (fds[i].fd == -1) continue;
            if (!(fds[i].revents & (POLLIN | POLLHUP | POLLERR))) continue;

            if (i == 0) {
                /* Слушающий сокет: новое подключение */
                int client_fd = accept(server_fd, NULL, NULL);
                if (client_fd == -1) {
                    if (errno == EINTR || errno == EAGAIN) continue;
                    perror("accept");
                    continue;
                }

                int slot = find_free_slot(fds, &nfds);
                if (slot < 0) {
                    fprintf(stderr, "[СЕРВЕР] Слишком много клиентов, "
                                    "отклоняю fd=%d\n", client_fd);
                    close(client_fd);
                    continue;
                }
                fds[slot].fd = client_fd;
                fds[slot].events = POLLIN;
                fds[slot].revents = 0;

                printf("[СЕРВЕР] Новый клиент подключен (fd=%d). Всего: %d\n",
                       client_fd, count_clients(fds, nfds));
                fflush(stdout);
            } else {
                /* Клиентский сокет: данные или отключение */
                int fd = fds[i].fd;
                ssize_t n = read(fd, buf, sizeof(buf));

                if (n > 0) {
                    for (ssize_t k = 0; k < n; k++) {
                        buf[k] = (char)toupper((unsigned char)buf[k]);
                    }
                    char prefix[64];
                    int plen = snprintf(prefix, sizeof(prefix),
                                        "[Клиент %d]: ", fd);
                    if (write_all(STDOUT_FILENO, prefix, (size_t)plen) == -1 ||
                        write_all(STDOUT_FILENO, buf, (size_t)n) == -1) {
                        perror("write");
                    }
                    /* если клиент не прислал '\n' — добавим для читаемости */
                    if (buf[n - 1] != '\n') {
                        write_all(STDOUT_FILENO, "\n", 1);
                    }
                } else if (n == 0) {
                    /* EOF — клиент отключился */
                    close(fd);
                    fds[i].fd = -1;
                    fds[i].events = 0;
                    fds[i].revents = 0;
                    printf("[СЕРВЕР] Клиент %d отключился.\n", fd);
                    printf("[СЕРВЕР] Осталось клиентов: %d\n",
                           count_clients(fds, nfds));
                    fflush(stdout);
                } else {
                    /* n == -1 */
                    if (errno == EINTR) continue;
                    perror("read");
                    close(fd);
                    fds[i].fd = -1;
                    fds[i].events = 0;
                    fds[i].revents = 0;
                    printf("[СЕРВЕР] Клиент %d отключился (ошибка). "
                           "Осталось клиентов: %d\n",
                           fd, count_clients(fds, nfds));
                    fflush(stdout);
                }
            }
        }

        /* Сжимаем nfds, если последние слоты пусты */
        while (nfds > 1 && fds[nfds - 1].fd == -1) {
            nfds--;
        }
    }

    /* Завершение по Ctrl+C */
    printf("\n[СЕРВЕР] Получен SIGINT. Закрываю соединения...\n");
    for (nfds_t i = 0; i < nfds; i++) {
        if (fds[i].fd != -1) close(fds[i].fd);
    }
    close(server_fd);
    unlink(SOCKET_PATH);
    printf("[СЕРВЕР] Завершён.\n");
    return EXIT_SUCCESS;
}