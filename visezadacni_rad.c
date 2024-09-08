#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <termios.h>
#include <errno.h>
#define MAXARGS 5
#define MAX_PROCESSES 100

struct sigaction prije;

pid_t active_processes[MAX_PROCESSES];
int process_count = 0;

void add_process(pid_t pid) {
    if (process_count < MAX_PROCESSES) {
        active_processes[process_count++] = pid;
    }
}

void remove_process(pid_t pid) {
    for (int i = 0; i < process_count; i++) {
        if (active_processes[i] == pid) {
            active_processes[i] = active_processes[process_count - 1];
            process_count--;
            return;
        }
    }
}

void obradi_dogadjaj(int sig) {
    printf("\n[signal SIGINT] proces %d primio signal %d\n", (int)getpid(), sig);
}

void obradi_signal_zavrsio_neki_proces_dijete(int sig) {
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        if (WIFEXITED(status)) {
            printf("[roditelj %d - SIGCHLD + waitpid] dijete %d zavrsilo s radom, status %d\n", (int)getpid(), pid, WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            printf("[roditelj %d - SIGCHLD + waitpid] dijete %d ubijeno signalom %d\n", (int)getpid(), pid, WTERMSIG(status));
        }
        remove_process(pid);
    }
}

pid_t pokreni_background_program(char *naredba[]) {
    pid_t pid = fork();
    if (pid == 0) {
        setsid();  // Napravi novu sesiju i postavi proces kao vođu sesije
        execvp(naredba[0], naredba);
        fprintf(stderr, "Neuspjelo izvršavanje %s\n", naredba[0]);
        
    } else if (pid > 0) {
        add_process(pid); // Dodajemo PID novog procesa u listu aktivnih procesa
    }
    return pid;
}

int main() {
    struct sigaction act;
    pid_t pid_novi;
    add_process((int)getpid());
    char buffer[128];
    size_t vel_buf = 128;

    printf("[roditelj %d] krenuo s radom\n", (int)getpid());

    act.sa_handler = obradi_dogadjaj;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    sigaction(SIGINT, &act, &prije);
    act.sa_handler = obradi_signal_zavrsio_neki_proces_dijete;
    sigaction(SIGCHLD, &act, NULL);
    act.sa_handler = SIG_IGN;
    sigaction(SIGTTOU, &act, NULL); 

    struct termios shell_term_settings;
    tcgetattr(STDIN_FILENO, &shell_term_settings);

    tcsetpgrp(STDIN_FILENO, getpgid(0));

    while (1) {
        printf("[roditelj] unesi naredbu: ");
        if (fgets(buffer, vel_buf, stdin) == NULL) break;
        if (buffer[0] == '\n') continue;

        char *argv[MAXARGS];
        int argc = 0;
        argv[argc] = strtok(buffer, " \t\n");
        while (argv[argc] != NULL) {
            argc++;
            argv[argc] = strtok(NULL, " \t\n");
        }

        if (argc == 0) continue; // Empty command was entered

        if (strcmp(argv[0], "cd") == 0) {
            if (argc < 2) {
                fprintf(stderr, "Nedostaje argument za cd\n");
            } else if (chdir(argv[1]) != 0) {
                perror("cd failed");
            }
        } else if (strcmp(argv[0], "exit") == 0) {
            break;
        } else if (strcmp(argv[0], "ps") == 0) {
            printf("PID\tNAREDBA\n");
            for (int i = 0; i < process_count; i++) {
                char proc_path[256];
                snprintf(proc_path, sizeof(proc_path), "/proc/%d/cmdline", active_processes[i]);
                FILE *f = fopen(proc_path, "r");
                if (f) {
                    char command[1024];
                    fgets(command, sizeof(command), f);
                    printf("%d\t%s\n", active_processes[i], command);
                    fclose(f);
                }
            }
        } else {
            if (argc > 1 && strcmp(argv[argc - 1], "&") == 0) {
                argv[argc - 1] = NULL;
                pid_novi = pokreni_background_program(argv);
                if (pid_novi > 0) {
                    printf("Pokrenut pozadinski proces %d\n", pid_novi);
                } else {
                    fprintf(stderr, "Neuspjelo pokretanje pozadinskog procesa\n");
                }
            } else {
                pid_novi = fork();
                if (pid_novi == 0) {
                    execvp(argv[0], argv);
                    fprintf(stderr, "Neuspjelo izvršavanje %s\n", argv[0]);
                    exit(1);
                } else if (pid_novi > 0) {
                    waitpid(pid_novi, NULL, 0);
                } else {
                    perror("fork failed");
                }
            }
        }
    }

    tcsetpgrp(STDIN_FILENO, getpgid(0));
    tcsetattr(STDIN_FILENO, TCSANOW, &shell_term_settings);
    return 0;
}