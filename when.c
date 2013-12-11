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
#include <time.h>
#include <errno.h>

#define VERSION "0.2.0"


typedef enum state {
  START = 1,
  /* User has cancelled request */
  CANCELLED,
  /* Launching conditional */
  LAUNCHING,
  /* Waiting for alarm, or completion of conditional */
  WAITING,
  /* Conditional died, and we got an alarm */
  RESTART,
  /* Alarm before conditional died */
  ALARM,
  /* We're ready to run finishing command */
  FINISHED
} state_t;

static state_t current_state = START;
static int alarm_time = 5;
static int success_when_zero = 0;
static int success_when_timebomb = 0;
static int verbose = 0;
static pid_t wait_for = 0;

static char *execargs[4] = {
  "/bin/sh",
  "-c",
  "",
  NULL
};

#define VERBOSE(format...) { if (verbose) { fprintf(stderr, format); } }

static void
timebomb_handler(int signum)
{
  switch (signum) {
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
      current_state = RESTART;
    }
    break;
  }
}

static void
zero_handler(int signum)
{
  if (signum == SIGINT) {
    current_state = CANCELLED;
  }
}

/**
 * Used in timebomb mode
 */
static void
chld_handler(int signum)
{
  /* When we get a sigchld, we wait for the alarm before
     trying again, but put ourselves into a holding state */
  if (signum == SIGCHLD) {
    current_state = WAITING;
  }
}

void
setup_sigchld(void)
{
  struct sigaction saction;
  sigemptyset(&saction.sa_mask);

  saction.sa_handler = chld_handler;
  saction.sa_flags = 0;

  if (sigaction(SIGCHLD, &saction, NULL)) {
    perror("sigaction");
    _exit(EXIT_FAILURE);
  }
}

void
setup_sighandlers(void)
{
  struct sigaction saction;

  sigemptyset(&saction.sa_mask);
  saction.sa_flags = 0;

  if (success_when_zero) {
    saction.sa_handler = zero_handler;
  }
  else {
    saction.sa_handler = timebomb_handler;

    if (sigaction(SIGALRM, &saction, NULL)) {
      perror("sigaction");
      _exit(EXIT_FAILURE);
    }
  }

  if (sigaction(SIGINT, &saction, NULL)) {
    perror("sigaction");
    _exit(EXIT_FAILURE);
  }

  if (success_when_timebomb) {
    setup_sigchld();
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
finish()
{
  sigset_t oset;
  pid_t finished_chld;
  int chld_status;

  if (current_state == ALARM || current_state == FINISHED) {
    finished_chld = fork();
    if (finished_chld < 0) {
      perror("fork");
      _exit(EXIT_FAILURE);
    }
    else if (finished_chld == 0) {
      if (execv(execargs[0], execargs) < 0) {
        perror("execv");
        _exit(EXIT_FAILURE);
      }
    }
    else {
      /**
       * We have an interesting problem. In the timebomb mode, we want to
       * keep <conditional> running until it completes. However, it may be
       * the case that <command> finishes quickly, which will signal a
       * sigchld, and terminate the `when` process, and ultimately, the
       * <conditional> that was originally running.
       *
       * The solution, for now (and this needs to be fixed), is to just
       * call waitpid() in the signal handler after we execute the finish
       * command.
       */

      VERBOSE("INFO: ignoring sigchld for finish\n");
      if (sigprocmask(SIG_SETMASK, NULL, &oset) == 0) {
        sigaddset(&oset, SIGCHLD);
        sigprocmask(SIG_BLOCK, &oset, NULL);
      }

      if (waitpid(wait_for, &chld_status, 0) < 0) {
        if (errno == ECHILD) {
          _exit(EXIT_SUCCESS);
        }
      }
      else {
        _exit(chld_status);
      }
    }
  }
}

static void
run_zero()
{
  int now;
  int last_time;
  int chld_status, status;

  VERBOSE("INFO: run in success when zero mode\n");

  do {
    switch (current_state) {
    case START:
    case RESTART:
      last_time = time(NULL);
      wait_for = fork();
      if (wait_for < 0) {
        perror("fork");
        _exit(EXIT_FAILURE);
      }
      else if (wait_for == 0) {
        VERBOSE("INFO: running %s\n", execargs[0]);
        if (execv(execargs[0], execargs) < 0) {
          perror("execv");
          _exit(EXIT_FAILURE);
        }
      }
      else {
        current_state = LAUNCHING;
        VERBOSE("INFO: waiting...\n");
        if (waitpid(wait_for, &chld_status, 0) >= 0) {
          status = WEXITSTATUS(chld_status);
          if (status == 0) {
            VERBOSE("INFO: FINISHED, will run finish command\n");
            current_state = FINISHED;
          }
          else {
            VERBOSE("INFO: > 0 exit code, WAITING for restart...\n");
            current_state = WAITING;
          }
        }
        else if (errno == ECHILD) {
          VERBOSE("INFO: ECHILD, switching to WAITING %d\n", chld_status);
          current_state = WAITING;
        }
      }
    case WAITING:
      /* sleep, hoping that we get some progress */
      msleep(10);
      now = time(NULL);
      if ((now - last_time) >= alarm_time) {
        /* process died, timeout occurred -- restart */
        VERBOSE("INFO: ok to RESTART\n");
        current_state = RESTART;
      }
      break;
    default:
      break;
    }
  }
  while (current_state != FINISHED &&
         current_state != CANCELLED);
}


static void
run_timebomb()
{

  VERBOSE("INFO: run in success when timebomb mode\n");

  do {
    switch (current_state) {
    case START:
    case RESTART:
      wait_for = fork();
      if (wait_for < 0) {
        perror("fork");
        _exit(EXIT_FAILURE);
      }
      else if (wait_for == 0) { /* launch conditional_cmd */
        VERBOSE("INFO: running %s\n", execargs[0]);
        if (execv(execargs[0], execargs) < 0) {
          perror("execv");
          _exit(EXIT_FAILURE);
        }
      }
      else {
        VERBOSE("INFO: in LAUNCHING state, setting an alarm\n");
        current_state = LAUNCHING;
        alarm(alarm_time);
      }
      break;
    case LAUNCHING:
    case WAITING:
      /* sleep, hoping that we get some progress */
      msleep(10);
      break;
    default:
      break;
    }
  } while (current_state != ALARM &&
           current_state != CANCELLED &&
           current_state != FINISHED);
}

void
usage(int argc, char **argv)
{
  fprintf(stderr, "usage: %s [-n seconds] "
          "[-h] [-t|-z] [-V] [-v] <condition> <finished>\n",
          argv[0]);
}


int
main(int argc, char **argv)
{
  char ch;

  if (argc > 1) {
    while ((ch = getopt(argc, argv, "hn:tvz")) != -1) {
      switch (ch) {
      case 'h':
        usage(argc, argv);
        _exit(EXIT_SUCCESS);
      case 'n':
        alarm_time = atoi(optarg);
        break;
      case 't':
        success_when_timebomb = 1;
        break;
      case 'v':
        printf("%s\n", VERSION);
        _exit(EXIT_SUCCESS);
      case 'V':
        verbose = 1;
        break;
      case 'z':
        success_when_zero = 1;
        break;
      default:
        usage(argc, argv);
        _exit(EXIT_FAILURE);
      }
    }

    if (success_when_timebomb && success_when_zero) {
      fprintf(stderr, "ERROR: can't use both timebomb and zero mode\n");
      usage(argc, argv);
      return 1;
    }

    setup_sighandlers();

    if ((argc - optind) == 2) {
      argv += optind;

      execargs[2] = strdup(argv[0]);
      if (success_when_timebomb) {
        run_timebomb();
      }
      else {
        run_zero();
      }

      if (current_state == FINISHED || current_state == ALARM) {
        execargs[2] = strdup(argv[1]);
        finish();
        _exit(EXIT_SUCCESS);
      }
    }
  }

  usage(argc, argv);
  return 0;
}
