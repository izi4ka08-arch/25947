#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <aio.h>
#include <sys/socket.h>
#include <sys/un.h>

#define SOCKET_PATH      "./socket"
#define BACKLOG          10
#define MAX_CLIENTS      100
#define POLL_INTERVAL_US 10000   /* 10 мс */

static volatile sig_atomic_t keep_running = 1;

struct client {
    int fd;
    int id;
    int active;
    char byte;
    struct aiocb cb;
};

static struct client clients[MAX_CLIENTS];
static int client_count = 0;
static int next_id = 1;

static void on_signal(int sig) { (void)sig; keep_running = 0; }

static void die(const char *msg) {
    perror(msg);
    unlink(SOCKET_PATH);
    exit(EXIT_FAILURE);
}

static int find_free_slot(void) {
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (clients[i].fd == -1) return i;
    return -1;
}

/* Поставить заявку на чтение 1 байта из сокета клиента */
static int arm_read(struct client *c) {
    memset(&c->cb, 0, sizeof(c->cb));
    c->cb.aio_fildes  = c->fd;
    c->cb.aio_buf     = &c->byte;
    c->cb.aio_nbytes  = 1;               /* именно 1 — требование Solaris */
    c->cb.aio_offset  = 0;
    c->cb.aio_sigevent.sigev_notify = SIGEV_NONE;  /* опрос через aio_error */

    if (aio_read(&c->cb) == -1) {
        if (errno == EAGAIN || errno == EINPROGRESS) return 0;  /* не ошибка */
        return -1;
    }
    return 0;
}

static void close_client(struct client *c, const char *reason) {
    if (c->fd == -1) return;
    aio_cancel(c->fd, &c->cb);
    close(c->fd);
    c->fd = -1;
    c->active = 0;
    client_count--;
    printf("[СЕРВЕР] Клиент %d отключился (%s). Осталось: %d\n",
           c->id, reason, client_count);
    fflush(stdout);
}

int main(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

    for (int i = 0; i < MAX_CLIENTS; i++) clients[i].fd = -1;

    if (unlink(SOCKET_PATH) == -1 && errno != ENOENT) {
        perror("unlink");
        return EXIT_FAILURE;
    }

    int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd == -1) die("socket");

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        close(server_fd); die("bind");
    }
    if (listen(server_fd, BACKLOG) == -1) {
        close(server_fd); die("listen");
    }

    /* Неблокирующий accept */
    int flags = fcntl(server_fd, F_GETFL, 0);
    fcntl(server_fd, F_SETFL, flags | O_NONBLOCK);

    printf("[СЕРВЕР] AIO-сервер запущен на %s\n", SOCKET_PATH);
    printf("[СЕРВЕР] Ctrl+C для остановки.\n");
    fflush(stdout);

    while (keep_running) {
        /* 1. Принимаем подключения */
        for (;;) {
            int nf = accept(server_fd, NULL, NULL);
            if (nf == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                if (errno == EINTR) continue;
                perror("accept");
                break;
            }
            /* Клиентский сокет должен быть блокирующим (требование Solaris) */
            int cf = fcntl(nf, F_GETFL, 0);
            if (cf != -1) fcntl(nf, F_SETFL, cf & ~O_NONBLOCK);

            int slot = find_free_slot();
            if (slot < 0) {
                fprintf(stderr, "[СЕРВЕР] Слишком много клиентов, "
                                "отклоняю fd=%d\n", nf);
                close(nf);
                continue;
            }
            clients[slot].fd = nf;
            clients[slot].id = next_id++;
            clients[slot].active = 1;
            clients[slot].byte = 0;
            client_count++;
            printf("[СЕРВЕР] Клиент %d подключён (slot=%d fd=%d). Всего: %d\n",
                   clients[slot].id, slot, nf, client_count);
            fflush(stdout);

            if (arm_read(&clients[slot]) == -1) {
                perror("aio_read");
                close_client(&clients[slot], "aio_read failed");
            }
        }

        /* 2. Опрос завершённых заявок */
        for (int i = 0; i < MAX_CLIENTS; i++) {
            struct client *c = &clients[i];
            if (c->fd == -1) continue;

            int err = aio_error(&c->cb);
            if (err == EINPROGRESS || err == EAGAIN) continue;  /* ещё идёт */
            if (err == ECANCELED) { close_client(c, "cancelled"); continue; }
            if (err != 0)         { close_client(c, strerror(err)); continue; }

            ssize_t n = aio_return(&c->cb);
            if (n > 0) {
                char ch = (char)toupper((unsigned char)c->byte);
                write(STDOUT_FILENO, &ch, 1);
                arm_read(c);           /* переставить заявку на след. байт */
            } else if (n == 0) {
                close_client(c, "EOF");
            } else {
                close_client(c, "read error");
            }
        }

        usleep(POLL_INTERVAL_US);
    }

    printf("\n[СЕРВЕР] Завершаюсь...\n");
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].fd != -1) {
            aio_cancel(clients[i].fd, &clients[i].cb);
            close(clients[i].fd);
        }
    }
    close(server_fd);
    unlink(SOCKET_PATH);
    return EXIT_SUCCESS;
}