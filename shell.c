
/* THINGS THIS SIMPLE SHELL CAN'T DO
 * - combination of or multiple redirection, piping, or & in a command
 * - piping, redirection, background with built in commands (redirection may
 *   work)
 * - && and || operators
 * - treating multi-word strings as one parameter
 * - running more than just a background task in a single line
 * - return "command not found" if command doesn't exist (for me I just return a
 *   blank line) producing more than 50 background tasks
 */

#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

const int MAX_NUM_BG_PROCESSES = 20;

int fg_pid = 0;  // in parent, 0 means there's no foreground process
                 // in child, it should always be 0
int fg_pid2 = 0; // piping involves creating two processes

int getcmd(
    char* prompt,
    char* line,
    char* args[],
    bool* bg,
    bool* redirect_out,
    bool* redirect_in,
    int* pipe_index
) {
    int length;
    int num_args = 0;
    char* token = NULL;
    char* loc = NULL;
    line = NULL;
    size_t alloc_size = 0; // unused

    printf("%s", prompt);
    length = getline(&line, &alloc_size, stdin);
    // pressing enter registers \n as the last character of the line
    if (length <= 0) {
        exit(EXIT_FAILURE);
    }

    // Check if background is specified..
    loc = index(line, '&');
    if (loc != NULL) {
        *bg = true;
        *loc = ' '; // removes the &
    } else {
        *bg = false;
    }

    // Check for redirection >, <
    loc = index(line, '>');
    if (loc != NULL) {
        *redirect_out = true;
        *loc = ' ';
    } else {
        *redirect_out = false;
    }
    loc = index(line, '<');
    if (loc != NULL) {
        *redirect_in = true;
        *loc = ' ';
    } else {
        *redirect_in = false;
    }

    char* to_be_parsed = line;
    while ((token = strsep(&to_be_parsed, " \t\n")) != NULL) {
        // deal with \r\n
        for (int j = 0; j < strlen(token); j++) {
            if (token[j] <= 32) {
                token[j] = '\0';
            }
        }

        if (strlen(token) > 0) {
            if (strcmp(token, "|") == 0) {
                *pipe_index = num_args;
                args[num_args] = NULL;
            } else {
                args[num_args] = token;
            }

            num_args++;
        }
    }

    args[num_args] = NULL;

    return num_args;
}

// Ctrl-Z
void handle_sigstop(int sig) {}

// Ctrl-C
void handle_sigint(int sig) {
    if (fg_pid > 0) { // parent and there's an fg process
        kill(fg_pid, SIGKILL);
    }
    if (fg_pid2 > 0) { // for piping
        kill(fg_pid2, SIGKILL);
    }
}

int main(void) {
    // SIGNAL HANDLING
    if (signal(SIGTSTP, handle_sigstop) == SIG_ERR
        || signal(SIGINT, handle_sigint) == SIG_ERR) {
        printf("ERROR! Could not bind the signal handler to parent. Exiting\n");
        exit(EXIT_FAILURE);
    }

    int bg_pids[MAX_NUM_BG_PROCESSES];
    char* bg_process_names[MAX_NUM_BG_PROCESSES];
    int num_bg_processes = 0;

    while (1) {
        char* line = NULL;
        char* args[50];
        bool bg = false;
        bool redirect_out = false;
        bool redirect_in = false;
        int my_stdout = 0;
        int my_stdin = 0;
        int fd_out = 0;
        int fd_in = 0;
        int pipe_index = 0;
        int num_args = getcmd(
            "\nsh > ", line, args, &bg, &redirect_out, &redirect_in, &pipe_index
        );

        if (args[0] == NULL) {
            goto free_line;
        }

        // OUTPUT REDIRECTION
        if (redirect_out) {
            my_stdout = dup(STDOUT_FILENO);
            close(STDOUT_FILENO);
            fd_out = open(
                args[num_args - 1], O_WRONLY | O_CREAT | O_TRUNC, 0644
            ); // O_TRUNC overwrites the file
            args[num_args - 1] = NULL;
        }
        // INPUT REDIRECTION
        if (redirect_in) {
            my_stdin = dup(STDIN_FILENO);
            close(STDIN_FILENO);
            fd_in = open(args[num_args - 1], O_RDONLY, 0644);
            args[num_args - 1] = NULL;
        }

        // BUILT-IN COMMANDS
        if (strcmp(args[0], "cd") == 0) {
            chdir(args[1]);
        } else if (strcmp(args[0], "pwd") == 0) {
            char* cwd = getcwd(NULL, 0);
            printf("%s\n", cwd);
            free(cwd);
        } else if (strcmp(args[0], "exit") == 0) {
            // exit out of background processes first
            for (int i = 0; i < num_bg_processes; i++) {
                kill(getpid(), SIGKILL);
            }
            exit(EXIT_SUCCESS); // bye bye!
        } else if (strcmp(args[0], "fg") == 0) {
            // - optional parameter: job index
            //   - default: index of the most recently created background
            //   process
            //   - index starts from 1
            // - finished bg processes keep their index

            int nth_job;
            if (args[1] == NULL) {
                if (num_bg_processes == 0) {
                    printf("No background jobs yet");
                    goto free_line;
                }

                // Some background processes are done. We want to go to the
                // first process that is still running.
                for (int i = num_bg_processes; i > 0; i--) {
                    if (waitpid(bg_pids[i - 1], NULL, WNOHANG) == 0) {
                        nth_job = i;
                        break;
                    }

                    if (i == 1) { // all the background processes are done
                        printf("No running background jobs yet");
                        goto free_line;
                    }
                }
            } else {
                nth_job = atoi(args[1]);
            }

            if (nth_job <= 0) {
                printf("Invalid argument");
            } else if (nth_job > num_bg_processes) {
                printf("Job number is only up to %d", num_bg_processes);
            } else {
                fg_pid = bg_pids[nth_job - 1];
                waitpid(bg_pids[nth_job - 1], NULL, 0);
            }
        } else if (strcmp(args[0], "jobs") == 0) {
            printf("Running - index\tpid\tname\n");
            for (int i = 0; i < num_bg_processes; i++) {
                if (waitpid(bg_pids[i], NULL, WNOHANG) == 0) { // still running
                    printf(
                        "\t  %i\t%i\t%s\n",
                        i + 1,
                        bg_pids[i],
                        bg_process_names[i]
                    );
                }
            }
        }
        // PIPING |
        else if (pipe_index != 0) {
            int p[2];
            if (pipe(p) == -1) {
                printf("Pipe error, exiting program\n");
                exit(EXIT_FAILURE);
            }

            fg_pid = fork();
            if (fg_pid == 0) {
                dup2(p[1], STDOUT_FILENO);
                close(p[0]);
                close(p[1]);
                execvp(args[0], args);
            } else if (fg_pid < 0) {
                printf("Failed to create child process for piping! Exiting "
                       "now.\n");
                exit(EXIT_FAILURE);
            }

            fg_pid2 = fork();
            if (fg_pid2 == 0) {
                dup2(p[0], STDIN_FILENO);
                close(p[0]);
                close(p[1]);
                execvp(*(args + pipe_index + 1), args + pipe_index + 1);
            } else if (fg_pid2 < 0) {
                printf("Failed to create child process for piping! Exiting "
                       "now.\n");
                exit(EXIT_FAILURE);
            }

            close(p[0]);
            close(p[1]);

            waitpid(fg_pid, NULL, 0);
            waitpid(fg_pid2, NULL, 0);
        }
        // NON-PIPING EXTERNAL COMMANDS
        // Fork a child and have the child run the command, if & is in command,
        // immediately prompt for next command. If
        else {
            fg_pid = fork();

            if (fg_pid < 0) {
                printf("Failed to create child process! Exiting now.\n");
                exit(EXIT_FAILURE);
            } else if (fg_pid == 0) {
                if (bg) {
                    // if i don't include this Ctrl-C will cancel all the
                    // processes
                    signal(SIGINT, SIG_IGN);
                }
                signal(SIGTSTP, SIG_IGN);

                if (execvp(args[0], args) == -1) {
                    printf("Error executing: %s\n", args[0]);
                    exit(EXIT_FAILURE);
                }
            } else {
                if (!bg) {
                    waitpid(fg_pid, NULL, 0);
                } else {
                    bg_pids[num_bg_processes] = fg_pid;
                    bg_process_names[num_bg_processes] = args[0];
                    num_bg_processes++;
                }
            }
        }

        fg_pid = 0;
        fg_pid2 = 0;

        // UNDO REDIRECTION
        if (redirect_out) {
            close(fd_out);
            dup(my_stdout);
            close(my_stdout);
        }
        if (redirect_in) {
            close(fd_in);
            dup(my_stdin);
            close(my_stdin);
        }

    free_line:
        if (line != NULL) {
            free(line);
        }
    }
}