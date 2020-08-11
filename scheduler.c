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
#define SCHED_TQ_SEC 2 /* time quantum */
#define TASK_NAME_SZ 60 /* maximum size for a task's name */
/*
 * SIGALRM handler
 */
struct process_info {
	int num;
	long int pid;
	int nextNum;
	int prevNum;
	char name[TASK_NAME_SZ];
};
struct process_info *t;
int id,nproc;
	static void
sigalrm_handler(int signum)
{
	//assert(0 && "Please fill me!");
	kill(t[id].pid, SIGSTOP);
}
/*
 * SIGCHLD handler
 */
	static void
sigchld_handler(int signum)
{
	pid_t p;
	int status,prev,next,i;
	//assert(0 && "Please fill me!");
	for(;;){
		p=waitpid(-1,&status,WUNTRACED|WNOHANG);
		if(p<0){
			perror("waitpid");
			exit(1);
		}
		if(p==0){
			break;
		}
		explain_wait_status(p,status);
		if(WIFEXITED(status)||WIFSIGNALED(status)){
			if(t[id].pid==p){
				printf("PID = %ld, id = %d, name= %s stopped its function\n",
						t[id].pid,t[id].num, t[id].name);
				prev=t[id].prevNum;
				next=t[id].nextNum;
				t[prev].nextNum=next;
				t[next].prevNum=prev;
				nproc--;
				if(nproc==0)exit(1);
				id=next;
				alarm(SCHED_TQ_SEC);
				kill(t[id].pid, SIGCONT);
			}
			else{
				i=t[id].nextNum;
				while(t[i].pid!=p){
					i=t[i].nextNum;
				}
				printf("PID = %ld, id = %d, name= %s stopped its function\n",
						t[i].pid,t[i].num, t[i].name);
				prev=t[i].prevNum;
				next=t[i].nextNum;
				t[prev].nextNum=next;
				t[next].prevNum=prev;
				nproc--;
				if(nproc==0)exit(1);
			}
		}
		if(WIFSTOPPED(status)){
			printf("PID = %ld, id = %d, name= %s paused\n",
					t[id].pid,t[id].num, t[id].name);
			id=t[id].nextNum;
			alarm(SCHED_TQ_SEC);
			kill(t[id].pid, SIGCONT);
		}
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
static void fork_child(char *executable){
	char *newargv[] = { executable, NULL, NULL, NULL };
	char *newenviron[] = { NULL };
	raise(SIGSTOP);
	execve(executable, newargv, newenviron);
	/* execve() only returns on error */
	perror("execve");
	exit(1);
}
int main(int argc, char *argv[])
{
	nproc=argc-1;
	t = malloc(nproc * sizeof(*t));
	pid_t pid;
	int i;
	/*
	 * For each of argv[1] to argv[argc - 1],
	 * create a new child process, add it to the process list.
	 */
	/*
	 * Start
	 */
	for(i=0;i<nproc;i++){
		pid = fork();
		if (pid < 0) {
			perror("main: fork");
			exit(1);
		}
		if (pid== 0) {
			/* Children*/
			fork_child(argv[i+1]);
		}
		printf("PID = %ld, id = %d name= %s is created\n",
				(long)pid, i, argv[i+1]);
		strcpy(t[i].name,argv[i+1]);
		t[i].num=i;
		t[i].pid=(long)pid;
		t[i].nextNum=(i+1)%nproc;
		if(i!=0)t[i].prevNum=i-1;
		else t[i].prevNum=nproc-1;
	}
	/* ... */
	/*
	 * Suspend Self
	 */
	//nproc = 0; /* number of processes goes here */
	/* Wait for all children to raise SIGSTOP before exec()ing. */
	wait_for_ready_children(nproc);
	/* Install SIGALRM and SIGCHLD handlers. */
	install_signal_handlers();
	if (nproc == 0) {
		fprintf(stderr, "Scheduler: No tasks. Exiting...\n");
		exit(1);
	}
	id=0;
	alarm(SCHED_TQ_SEC);
	kill(t[id].pid, SIGCONT);
	/* loop forever until we exit from inside a signal handler. */
	while (pause())
		;
	/* Unreachable */
	fprintf(stderr, "Internal error: Reached unreachable point\n");
	return 1;
}
