#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>

static volatile sig_atomic_t beep_count = 0;
static volatile sig_atomic_t quit_requested = 0;

static void sigint_handler(int sig)
{
    (void)sig;

    /* \a — звуковой сигнал терминала */
    const char bell = '\a';
    write(STDOUT_FILENO, &bell, 1);

    beep_count++;
}

static void sigquit_handler(int sig)
{
    (void)sig;
    quit_requested = 1;
}

static void install_handler(int sig, void (*handler)(int), int flags)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = flags;

    if (sigaction(sig, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
}

int main(void)
{
    sigset_t block_set, wait_set, old_set;

    /*
     * Блокируем SIGINT и SIGQUIT на время проверки флага,
     * чтобы не потерять сигнал между проверкой и sigsuspend().
     */
    sigemptyset(&block_set);
    sigaddset(&block_set, SIGINT);
    sigaddset(&block_set, SIGQUIT);

    if (sigprocmask(SIG_BLOCK, &block_set, &old_set) == -1) {
        perror("sigprocmask");
        return EXIT_FAILURE;
    }

    install_handler(SIGINT, sigint_handler, 0);
    install_handler(SIGQUIT, sigquit_handler, 0);

    sigemptyset(&wait_set);

    printf("Пищалка запущена!\n");
    printf("Нажмите Ctrl+C — будет звуковой сигнал\n");
    printf("Нажмите Ctrl+\\ — программа завершится с подсчётом сигналов\n");
    printf("Ожидаю сигналов...\n");
    fflush(stdout);

    while (!quit_requested) {
        /*
         * sigsuspend атомарно разблокирует SIGINT/SIGQUIT
         * и усыпляет процесс до прихода сигнала.
         */
        sigsuspend(&wait_set);
    }

    /* Восстанавливаем исходную маску сигналов */
    sigprocmask(SIG_SETMASK, &old_set, NULL);

    char buffer[128];
    int n = snprintf(buffer, sizeof(buffer),
                     "\nВсего прозвучало сигналов: %ld\n"
                     "Программа завершена по SIGQUIT\n",
                     (long)beep_count);

    if (n > 0) {
        write(STDOUT_FILENO, buffer, (size_t)n);
    }

    return EXIT_SUCCESS;
}