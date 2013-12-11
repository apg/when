/**
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 */

#include <sys/wait.h>
#include <sys/types.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum state {
  START = 1,
  CANCELLED,
  LAUNCHING,
  WAITING,
  DIED,
  ALARM
} state_t;

static state_t current_state = START;
static int alarm_time = 5;
static pid_t wait_for = 0;

static void
signal_handler(int signum)
{
  int status;
  switch (signum) {
  case SIGCHLD:
    /* When we get a sigchld, we wait for the alarm before
       trying again, but put ourselves into a holding state */
    current_state = WAITING;
    break;
  case SIGINT:
    current_state = CANCELLED;
    alarm(0);
    break;
  case SIGALRM:
    if (current_state != WAITING) {
      /* if we get an alarm before a SIGCHLD, we win */
      current_state = ALARM;
    }
    else {
      /* The SIGCHLD set us into the WAITING state, but now we got an
         alarm, which puts us in the DIED state. Re-launch imminent */
      current_state = DIED;
    }
    break;
  }

  if (wait_for) {
    waitpid(wait_for, &status, 0);
  }
}


static void
msleep(long ms)
{
  struct timespec tm;
  tm.tv_sec = ms / 1000;
  tm.tv_nsec = (ms % 1000) * 1000000;
  nanosleep(&tm, NULL); // if there's an error, I don't care here.
}

static void
run(char *condition_cmd, char *finish_cmd)
{
  char *newargs[4];
  pid_t chld, finished_chld;
  int chld_status;

  /* setup args */
  newargs[0] = "/bin/sh";
  newargs[1] = "-c";
  newargs[2] = strdup(condition_cmd);
  newargs[3] = 0;

  do {
    switch (current_state) {
    case START:
    case DIED:
      chld = fork();
      if (chld < 0) {
        perror("fork");
        _exit(EXIT_FAILURE);
      }
      else if (chld == 0) { /* launch conditional_cmd */
        if (execv(newargs[0], newargs) < 0) {
          perror("execv");
          _exit(EXIT_FAILURE);
        }
      }
      else {
        current_state = LAUNCHING;
        alarm(alarm_time);
      }
      break;
    case LAUNCHING:
    case WAITING:
      /* sleep, hoping that we get some progress */
      msleep(100);
      break;
    default:
      break;
    }
  } while (current_state != ALARM && current_state != CANCELLED);

  /* finish up! */
  if (current_state == ALARM) {
    finished_chld = fork();
    if (finished_chld < 0) {
      perror("fork");
      _exit(EXIT_FAILURE);
    }
    else if (finished_chld == 0) {
      /* need a new command */
      newargs[2] = strdup(finish_cmd);

      if (execv(newargs[0], newargs) < 0) {
        perror("execv");
        _exit(EXIT_FAILURE);
      }
    }
    else {
      /*
        We wait here, but when the finish cmd happens, we get a sigchld
        which breaks us out of the wait. For now, we establish a pid_t to
        wait for, and do so in the signal handler.

        In reality, we should probably block the SIGCHLD signal here and
        go forth, or put ourselves in a wait loop.
      */
      wait_for = chld;
      waitpid(wait_for, &chld_status, 0);
    }
  }
  else if (current_state == CANCELLED) {
    fprintf(stderr, "User aborted!\n");
    _exit(EXIT_FAILURE);
  }
}

void
usage(int argc, char **argv)
{
  fprintf(stderr, "usage: %s [-n alarmtime] <condition> <finished>\n",
         argv[0]);
  _exit(EXIT_FAILURE);

}

int
main(int argc, char **argv)
{
  char ch;
  struct sigaction saction;

  sigemptyset(&saction.sa_mask);
  saction.sa_flags = 0;
  saction.sa_handler = signal_handler;

  if (sigaction(SIGCHLD, &saction, NULL)) {
    perror("sigaction");
    _exit(EXIT_FAILURE);
  }

  if (sigaction(SIGALRM, &saction, NULL)) {
    perror("sigaction");
    _exit(EXIT_FAILURE);
  }

  if (sigaction(SIGINT, &saction, NULL)) {
    perror("sigaction");
    _exit(EXIT_FAILURE);
  }

  if (argc > 2) {
    while ((ch = getopt(argc, argv, "n:")) != -1) {
      switch (ch) {
      case 'n':
        alarm_time = atoi(optarg);
        break;
      default:
        usage(argc, argv);
      }
    }
    argc -= optind;
    argv += optind;

    run(argv[0], argv[1]);
  }
  else {
    usage(argc, argv);
  }
  return 0;
}
