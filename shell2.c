#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

int fg_pid = 0;
int fg_pid2 = 0;

char* line = NULL;

int getcmd(char* prompt, char* args[], int* background, int* redirect,
           int* pipe_index) {
  int length;
  int i = 0;
  char* token = NULL;
  char* loc = NULL;
  line = NULL;
  size_t alloc_size = 0;

  printf("%s", prompt);
  length = getline(&line, &alloc_size, stdin);
  if (length <= 0) {
    exit(-1);
  }

  loc = index(line, '&');
  if (loc != NULL) {
    *background = 1;
    *loc = ' ';
  } else {
    *background = 0;
  }

  char* line2 = line;
  while ((token = strsep(&line2, " \t\n")) != NULL) {
    for (int j = 0; j < strlen(token); j++) {
      if (token[j] <= 32) {
        token[j] = '\0';
      }
    }

    if (strlen(token) > 0) {
      if (strcmp(token, "|") == 0) {
        *pipe_index = i;
        args[i] = NULL;
      } else {
        args[i] = token;
      }

      i++;
    }
  }

  args[i] = NULL;

  return i;
}

void handle_sigstop(int sig) {}

void handle_sigint(int sig) {
  if (fg_pid > 0) {
    kill(fg_pid, SIGKILL);
  }
  if (fg_pid2 > 0) {
    kill(fg_pid2, SIGKILL);
  }
}

int main(void) {
  if (signal(SIGTSTP, handle_sigstop) == SIG_ERR ||
      signal(SIGINT, handle_sigint) == SIG_ERR) {
    printf("ERROR! Could not bind the signal handler to parent. Exiting\n");
    exit(EXIT_FAILURE);
  }

  int bg_pids[20];
  char* bg_names[20];
  int nth_bg_pid = 0;

  while (1) {
    char* args[50];
    int bg = 0;
    int redirect_out = 0;
    int stdout = 0;
    int fd = 0;
    int pipe_index = 0;
    int num_args = getcmd("\nsh > ", args, &bg, &redirect_out, &pipe_index);

    if (args[0] == NULL) {
    } else {
      if (strcmp(args[0], "cd") == 0) {
        if (redirect_out) {
          stdout = dup(STDOUT_FILENO);
          close(STDOUT_FILENO);
          fd = open(args[num_args - 1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
          args[num_args - 1] = NULL;
        }

        if (strcmp(args[0], "cd") == 0) {
          chdir(args[1]);
        } else if (strcmp(args[0], "pwd") == 0) {
          char* cwd = getcwd(NULL, 0);
          printf("%s\n", cwd);
          free(cwd);
        } else if (strcmp(args[0], "exit") == 0) {
          for (int i = 0; i < nth_bg_pid; i++) {
            kill(bg_pids[i], SIGKILL);
          }
          exit(0);
        } else if (strcmp(args[0], "fg") == 0) {
          int nth_job = nth_bg_pid;

          if (args[1] == NULL) {
            if (nth_job == 0) {
              nth_job = 1;
            } else {
              for (int i = nth_bg_pid; i > 0; i--) {
                if (waitpid(bg_pids[i - 1], NULL, WNOHANG) == 0) {
                  nth_job = i;
                  break;
                }
              }
            }
          } else {
            nth_job = atoi(args[1]);
          }

          if (nth_job == 0) {
            printf("Invalid argument");
          } else if (nth_bg_pid == 0) {
            printf("No background jobs yet");
          } else if (nth_job > nth_bg_pid) {
            printf("Job number is only up to %d", nth_bg_pid);
          } else {
            fg_pid = bg_pids[nth_job - 1];
            waitpid(bg_pids[nth_job - 1], NULL, 0);
          }
        } else if (strcmp(args[0], "jobs") == 0) {
          printf("Running - index\tpid\tname\tn");
          for (int i = 0; i < nth_bg_pid; i++) {
            if (waitpid(bg_pids[i], NULL, WNOHANG) == 0) {
              printf("\t %i\t%i\t%s\n", i + 1, bg_pids[i], bg_names[i]);
            }
          }
        } else if (pipe_index != 0) {
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
            printf("Failed to create child process for piping! Exiting now.\n");
            exit(EXIT_FAILURE);
          }

          fg_pid2 = fork();
          if (fg_pid2 == 0) {
            dup2(p[0], STDIN_FILENO);
            close(p[0]);
            close(p[1]);
            execvp(*(args + pipe_index + 1), args + pipe_index + 1);
          } else if (fd_pid2 < 0) {
            printf(
                "Failed to create child processes for piping! Exiting now.\n");
            exit(EXIT_FAILURE);
          }
        } else {
          fg_pid = fork();

          if (fg_pid < 0) {
            printfc("Failed to create child process! Exiting now.\n");
            exit(EXIT_FAILURE);
          } else if (fg_pid == 0) {
            if (bg == 1) {
              signal(SIGINT, SIG_IGN);
            }
            signal(SIGTSTP, SIG_IGN);

            if (execvp(args[0], args) == -1) {
              printf("Error executing: %s\n", args[0]);
              exit(EXIT_FAILURE);
            }
          } else {
            if (bg == 0) {
              waitpid(fg_pid, NULL, 0);
            } else {
              bg_pids[nth_bg_pid] = fg_pid;
              bg_names[nth_bg_pid] = args[0];
              nth_bg_pid++;
            }
          }
        }

        fg_pid = 0;
        fg_pid2 = 0;

        if (redirect_out == 1) {
          close(fd);
          dup(stdout);
          close(stdout);
        }

        if (line != NULL) {
          free(line);
        }
      }
    }
  }