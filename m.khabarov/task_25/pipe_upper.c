#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <sys/wait.h>

#define BUF_SIZE 4096

static void child_process(int read_fd)
{
    char buf[BUF_SIZE];
    ssize_t n;

    printf("[CHILD] Запущен. Ожидаю данные от родителя...\n");
    fflush(stdout);

    while ((n = read(read_fd, buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < n; i++) {
            buf[i] = (char)toupper((unsigned char)buf[i]);
        }
        if (write(STDOUT_FILENO, "[CHILD] Получено и преобразовано: ",
                  sizeof("[CHILD] Получено и преобразовано: ") - 1) < 0) {
            perror("write");
            break;
        }
        if (write(STDOUT_FILENO, buf, (size_t)n) < 0) {
            perror("write");
            break;
        }
        /* Если родитель не прислал '\n' — добавим для читаемости */
        if (n > 0 && buf[n - 1] != '\n') {
            write(STDOUT_FILENO, "\n", 1);
        }
    }

    if (n < 0) {
        perror("read");
    }

    close(read_fd);
    printf("[CHILD] Конец данных (EOF). Завершаюсь.\n");
    fflush(stdout);
    _exit(EXIT_SUCCESS);
}

int main(void)
{
    int pipefd[2];
    pid_t pid;

    if (pipe(pipefd) == -1) {
        perror("pipe");
        return EXIT_FAILURE;
    }

    pid = fork();
    if (pid == -1) {
        perror("fork");
        close(pipefd[0]);
        close(pipefd[1]);
        return EXIT_FAILURE;
    }

    if (pid == 0) {
        /* Потомок: читает из канала */
        close(pipefd[1]);          /* закрываем ненужный конец записи */
        child_process(pipefd[0]);
        /* сюда не попадём: child_process вызывает _exit */
    }

    /* Родитель: пишет в канал */
    close(pipefd[0]);              /* закрываем ненужный конец чтения */

    printf("[PARENT] Связь через канал установлена!\n");
    printf("[PARENT] Введите текст (пустая строка или Ctrl+D — выход):\n");
    fflush(stdout);

    char line[BUF_SIZE];
    while (1) {
        printf("> ");
        fflush(stdout);

        if (fgets(line, sizeof(line), stdin) == NULL) {
            printf("\n[PARENT] Получен EOF (Ctrl+D) — завершаю ввод.\n");
            break;
        }

        /* Убираем перевод строки из проверки на пустоту */
        line[strcspn(line, "\n")] = '\0';

        if (line[0] == '\0') {
            printf("[PARENT] Пустая строка — завершаю ввод.\n");
            break;
        }

        /* Возвращаем '\n' и отправляем в канал */
        size_t len = strlen(line);
        line[len] = '\n';
        len++;

        ssize_t written = 0;
        while (written < (ssize_t)len) {
            ssize_t w = write(pipefd[1], line + written,
                              (size_t)(len - written));
            if (w == -1) {
                if (errno == EINTR) continue;
                perror("write");
                close(pipefd[1]);
                wait(NULL);
                return EXIT_FAILURE;
            }
            written += w;
        }
    }

    /* Закрытие конца записи даёт потомку EOF */
    close(pipefd[1]);

    int status;
    if (wait(&status) == -1) {
        perror("wait");
        return EXIT_FAILURE;
    }

    if (WIFEXITED(status)) {
        printf("[PARENT] Потомок завершился с кодом %d. "
               "Родитель тоже завершает работу.\n", WEXITSTATUS(status));
    } else {
        printf("[PARENT] Потомок завершился аварийно. "
               "Родитель тоже завершает работу.\n");
    }

    return EXIT_SUCCESS;
}