#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>


#define VERSION "0.2.1"

typedef enum state {
  START = 1,
  CANCELLED,
  LAUNCHING,
  WAITING,
  RESTART,
  FINISHED
} state_t;


static state_t current_state = START;
static int verbose = 0;
static long alarm_time = 5;

static char *exec_args[4] = {
  "/bin/sh",
  "-c",
  "",
  NULL
};


static void
msleep(long ms)
{
  struct timespec tm;
  tm.tv_sec = ms / 1000;
  tm.tv_nsec = (ms % 1000) * 1000000;
  nanosleep(&tm, NULL);
}


static int
run(char *command)
{
  int now, last_time, chld_status;
  pid_t chld;

  if (verbose) {
    fprintf(stderr, "INFO: running at interval %ld until success\n",
            alarm_time);
  }

  exec_args[2] = strdup(command);

  do {
    switch (current_state) {
    case START:
    case RESTART:
      last_time = time(NULL);
      chld = fork();
      if (chld < 0) {
        perror("fork");
        _exit(EXIT_FAILURE);
      }
      else if (chld == 0) {
        if (verbose) {
          fprintf(stderr, "INFO: running %s in subshell\n", exec_args[2]);
        }

        if (execv(exec_args[0], exec_args) < 0) {
          perror("execv");
          _exit(EXIT_FAILURE);
        }
      }
      else {
        current_state = WAITING;
        if (verbose) {
          fprintf(stderr, "INFO: switched to WAITING\n");
        }
      rewait:
        if (waitpid(-1, &chld_status, 0) >= 0) {
          if (WEXITSTATUS(chld_status) == 0) {
            if (verbose) {
              fprintf(stderr, "INFO: FINISHED\n");
            }

            current_state = FINISHED;
          }
          else {
            if (verbose) {
              fprintf(stderr, "INFO: non-zero exit status, "
                      "WAITING for restart\n");
            }
            current_state = WAITING;
          }
        }
        else {
          if (errno == EINTR) {
            goto rewait;
          }
          if (errno == ECHILD) {
            if (verbose) {
              fprintf(stderr, "INFO: FINISHED - 2\n");
            }
            current_state = FINISHED; /* no more children */
          }
        }
      }

      break;

    case WAITING:
      msleep(10);
      now = time(NULL);
      if ((now - last_time) >= alarm_time) {
        if (verbose) {
          fprintf(stderr, "INFO: finished WAITING, RESTARTING\n");
        }

        current_state = RESTART;
      }

      break;

    default:
      break;
    }
  }
  while (current_state != FINISHED &&
         current_state != CANCELLED);

  return 0;
}


static void
usage(char *prog)
{
  fprintf(stderr, "usage: %s [-n seconds] [-hvV] <command>\n", prog);
}


int
main(int argc, char **argv)
{
  char ch;
  int i, ci, l;
  char command[1024];
  char *current;

  if (argc > 1) {
    while ((ch = getopt(argc, argv, "+hn:vVx")) != -1) {
      switch (ch) {
      case 'h':
        usage(argv[0]);
        exit(EXIT_SUCCESS);
        break;
      case 'n':
        alarm_time = strtol(optarg, NULL, 10);
        if ((errno == ERANGE &&
             (alarm_time == LONG_MAX || alarm_time == LONG_MIN))
            || alarm_time <= 0) {
          fprintf(stderr, "ERROR: invalid seconds argument\n");
          usage(argv[0]);
          exit(EXIT_FAILURE);
        }
        break;
      case 'v':
        printf("%s\n", VERSION);
        exit(EXIT_SUCCESS);
        break;
      case 'V':
        verbose = 1;
        break;
      default:
        usage(argv[0]);
        exit(EXIT_FAILURE);
      }
    }

    /* get command */
    current = command;
    ci = 0;
    for (i = optind; i < argc; i++) {
      l = strlen(argv[i]);
      if ((ci + l + 1) < 1023) {
        if (i != optind) {
          current[ci++] = ' ';
        }
        strncpy(current + ci, argv[i], l);
        ci += l;
      }
      else {
        fprintf(stderr, "ERROR: command won't fit in buffer\n");
        /* TODO: here is why dynamic allocation would be good */
        exit(EXIT_FAILURE);
      }
    }
    current[ci] = '\0';

    return run(command);
  }
  usage(argv[0]);
  return 0;
}
