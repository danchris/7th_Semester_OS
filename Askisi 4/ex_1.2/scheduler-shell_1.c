#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <assert.h>

#include <sys/wait.h>
#include <sys/types.h>

#include "proc-common.h"
#include "request.h"

/* Compile-time parameters. */
#define SCHED_TQ_SEC 2                /* time quantum */
#define TASK_NAME_SZ 60               /* maximum size for a task's name */
#define SHELL_EXECUTABLE_NAME "shell" /* executable for shell */

typedef struct node {
    int id;
    char *name;
    pid_t p;
    struct node *prev;
    struct node *next;
} node_t;
int counter=0;
node_t *running=NULL, *head=NULL;

node_t *insertToEmpty ( node_t *current, pid_t p, int id, char *name) {
    if( current !=NULL )
        return current;

    node_t *c = (node_t*)malloc(sizeof(node_t));
    c->id = id;
    c->name = strdup(name);
    c->p = p;
    c->next = c;
    c->prev = c;
    current = c;

    return current;
}

node_t *insertToList(node_t *current, pid_t p, int id, char *name){
  if (current == NULL)
     return insertToEmpty(current, p, id, name);
  node_t *temp = (node_t *)malloc(sizeof(node_t));
  temp->id = id;
  temp->name = strdup(name);
  temp->p = p;
  temp -> next = current -> next;
  current -> next = temp;
  current = temp;

  return current;
}

void deleteFromList (pid_t p){
    if(head==NULL)
        return;

    node_t *curr = head, *prev=NULL;

    while (curr->p != p ) {
        if (curr->next == head)
            break;
        prev = curr;
        curr = curr->next;
    }
   if (curr==head){
        prev = head;
        while(prev->next != head)
            prev = prev ->next;
        head = curr->next;
        prev->next = head;
        free(curr);
    }
    else if (curr->next == head){
        prev->next = head;
        free(curr);
    }
    else {
        prev->next = curr->next;
        free(curr);
    }
}

/* Print a list of all tasks currently being scheduled.  */
static void
sched_print_tasks(void)
{
    node_t *temp = head;
    do{
        printf("\t\t\t\tID = %d\t|\tName = %s\t|\tPID = %d", temp->id,temp->name,temp->p);
        if (temp==running) printf("\tRunning...");
        printf("\n");
        temp = temp->next;
    }while(temp!=head);
}

/* Send SIGKILL to a task determined by the value of its
 * scheduler-specific id.
 */
static int
sched_kill_task_by_id(int id)
{
    node_t *temp = head;
    while(temp->id!=id){
        if(temp->next == head) {
            printf("Error: Don't found this process\n");
            break;
        }
        temp = temp->next;
    }

    deleteFromList(temp->p);
    kill(temp->p,SIGKILL);
	return -ENOSYS;
}


/* Create a new task.  */
static void
sched_create_task(char *executable)
{
	char *newargv[] = { executable, NULL, NULL, NULL };
	char *newenviron[] = { NULL };
    pid_t p;
    p = fork();
    if (p < 0) {
        /* fork failed */
        perror("fork");
        exit(1);
    }

    if (p == 0) {
        raise(SIGSTOP);
        execve(executable, newargv, newenviron);
        /* execve() only returns on error */
        perror("execve");
        assert(0);
    }
    else{
        running = insertToList(head->prev,p,++counter,executable);
    }

}

/* Process requests by the shell.  */
static int
process_request(struct request_struct *rq)
{
	switch (rq->request_no) {
		case REQ_PRINT_TASKS:
			sched_print_tasks();
			return 0;

		case REQ_KILL_TASK:
			return sched_kill_task_by_id(rq->task_arg);

		case REQ_EXEC_TASK:
			sched_create_task(rq->exec_task_arg);
			return 0;

		default:
			return -ENOSYS;
	}
}

/*
 * SIGALRM handler
 */
static void
sigalrm_handler(int signum)
{
	if (signum != SIGALRM) {
		fprintf(stderr, "Internal error: Called for signum %d, not SIGALRM\n",
			signum);
		exit(1);
	}

    /* Edw prepei na stamataw thn trexousa diergasia */
	printf("ALARM! %d seconds have passed.\n", SCHED_TQ_SEC);
    kill(running->p,SIGSTOP);

}

/*
 * SIGCHLD handler
 */
static void
sigchld_handler(int signum)
{
	pid_t p;
	int status;

	if (signum != SIGCHLD) {
		fprintf(stderr, "Internal error: Called for signum %d, not SIGCHLD\n",
			signum);
		exit(1);
	}

	for (;;) {
		p = waitpid(-1, &status, WUNTRACED | WNOHANG);
		if (p < 0) {
			perror("waitpid");
			exit(1);
		}
		if (p == 0)
			break;

		explain_wait_status(p, status);

        if (WIFEXITED(status) || WIFSIGNALED(status)) {
			/* A child has died */
			printf("Parent: Received SIGCHLD, child is dead. Exiting.\n");
            if (p==running->p) deleteFromList(running->p);
		}

		if (WIFSTOPPED(status)) {
			/* A child has stopped due to SIGSTOP/SIGTSTP, etc... */
			printf("Parent: Child has been stopped. Moving right along...\n");
		}
        running = running->next;
        alarm(SCHED_TQ_SEC);
        printf("Child with pid = %d will continue\n", running->p);
        kill(running->p,SIGCONT);
	}
}

/* Disable delivery of SIGALRM and SIGCHLD. */
static void
signals_disable(void)
{
	sigset_t sigset;

	sigemptyset(&sigset);
	sigaddset(&sigset, SIGALRM);
	sigaddset(&sigset, SIGCHLD);
	if (sigprocmask(SIG_BLOCK, &sigset, NULL) < 0) {
		perror("signals_disable: sigprocmask");
		exit(1);
	}
}

/* Enable delivery of SIGALRM and SIGCHLD.  */
static void
signals_enable(void)
{
	sigset_t sigset;

	sigemptyset(&sigset);
	sigaddset(&sigset, SIGALRM);
	sigaddset(&sigset, SIGCHLD);
	if (sigprocmask(SIG_UNBLOCK, &sigset, NULL) < 0) {
		perror("signals_enable: sigprocmask");
		exit(1);
	}
}


/* Install two signal handlers.
 * One for SIGCHLD, one for SIGALRM.
 * Make sure both signals are masked when one of them is running.
 */
static void
install_signal_handlers(void)
{
	sigset_t sigset;
	struct sigaction sa;

	sa.sa_handler = sigchld_handler;
	sa.sa_flags = SA_RESTART;
	sigemptyset(&sigset);
	sigaddset(&sigset, SIGCHLD);
	sigaddset(&sigset, SIGALRM);
	sa.sa_mask = sigset;
	if (sigaction(SIGCHLD, &sa, NULL) < 0) {
		perror("sigaction: sigchld");
		exit(1);
	}

	sa.sa_handler = sigalrm_handler;
	if (sigaction(SIGALRM, &sa, NULL) < 0) {
		perror("sigaction: sigalrm");
		exit(1);
	}

	/*
	 * Ignore SIGPIPE, so that write()s to pipes
	 * with no reader do not result in us being killed,
	 * and write() returns EPIPE instead.
	 */
	if (signal(SIGPIPE, SIG_IGN) < 0) {
		perror("signal: sigpipe");
		exit(1);
	}
}

static void
do_shell(char *executable, int wfd, int rfd)
{
	char arg1[10], arg2[10];
	char *newargv[] = { executable, NULL, NULL, NULL };
	char *newenviron[] = { NULL };

	sprintf(arg1, "%05d", wfd);
	sprintf(arg2, "%05d", rfd);
	newargv[1] = arg1;
	newargv[2] = arg2;

	raise(SIGSTOP);
	execve(executable, newargv, newenviron);

	/* execve() only returns on error */
	perror("scheduler: child: execve");
	exit(1);
}

/* Create a new shell task.
 *
 * The shell gets special treatment:
 * two pipes are created for communication and passed
 * as command-line arguments to the executable.
 */
static void
sched_create_shell(char *executable, int *request_fd, int *return_fd)
{
	pid_t p;
	int pfds_rq[2], pfds_ret[2];

	if (pipe(pfds_rq) < 0 || pipe(pfds_ret) < 0) {
		perror("pipe");
		exit(1);
	}

	p = fork();
	if (p < 0) {
		perror("scheduler: fork");
		exit(1);
	}

	if (p == 0) {
		/* Child */
		close(pfds_rq[0]);
		close(pfds_ret[1]);
		do_shell(executable, pfds_rq[1], pfds_ret[0]);
		assert(0);
	}
    head = insertToEmpty(head,p,++counter,SHELL_EXECUTABLE_NAME);
	/* Parent */
	close(pfds_rq[1]);
	close(pfds_ret[0]);
	*request_fd = pfds_rq[0];
	*return_fd = pfds_ret[1];
}

static void
shell_request_loop(int request_fd, int return_fd)
{
	int ret;
	struct request_struct rq;

	/*
	 * Keep receiving requests from the shell.
	 */
	for (;;) {
		if (read(request_fd, &rq, sizeof(rq)) != sizeof(rq)) {
			perror("scheduler: read from shell");
			fprintf(stderr, "Scheduler: giving up on shell request processing.\n");
			break;
		}

		signals_disable();
		ret = process_request(&rq);
		signals_enable();

		if (write(return_fd, &ret, sizeof(ret)) != sizeof(ret)) {
			perror("scheduler: write to shell");
			fprintf(stderr, "Scheduler: giving up on shell request processing.\n");
			break;
		}
	}
}

int main(int argc, char *argv[])
{
	int nproc;
    pid_t p;
	/* Two file descriptors for communication with the shell */
	static int request_fd, return_fd;
    node_t *curr = NULL;
	char *executable = malloc(sizeof(char *));
	char *newargv[] = { executable, NULL, NULL, NULL };
	char *newenviron[] = { NULL };

	/* Create the shell. */
	sched_create_shell(SHELL_EXECUTABLE_NAME, &request_fd, &return_fd);
    curr = head;
	/* TODO: add the shell to the scheduler's tasks */

	/*
	 * For each of argv[1] to argv[argc - 1],
	 * create a new child process, add it to the process list.
	 */
	nproc = argc - 1; /* number of proccesses goes here */


    for(int i = 1; i <= nproc; i++){

        p = fork();
        if (p < 0) {
            /* fork failed */
            perror("fork");
            exit(1);
        }

        if (p == 0) {
            raise(SIGSTOP);
            strcpy(executable,argv[i]);
            execve(executable, newargv, newenviron);
            /* execve() only returns on error */
            perror("execve");
            assert(0);
        }
        else{
            curr = insertToList(curr,p,++counter,argv[i]);
        }
    }

	/* Wait for all children to raise SIGSTOP before exec()ing. */
	wait_for_ready_children(nproc);

	/* Install SIGALRM and SIGCHLD handlers. */
	install_signal_handlers();

	if (nproc == 0) {
		fprintf(stderr, "Scheduler: No tasks. Exiting...\n");
		exit(1);
	}
    running = head;
    alarm(SCHED_TQ_SEC);
    kill(running->p,SIGCONT);
	shell_request_loop(request_fd, return_fd);

	/* Now that the shell is gone, just loop forever
	 * until we exit from inside a signal handler.
	 */
	while (pause())
		;

	/* Unreachable */
	fprintf(stderr, "Internal error: Reached unreachable point\n");
	return 1;
}
