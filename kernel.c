#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <ucontext.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "syscalls.h"


/*
 *
 * Kernel variables
 *
 */
enum ProcState { READY, SLEEPING, FINISHED };

typedef struct PCB_s {
	Pid_t parent_pid;
	Pid_t pid;
	int exitvalue;
	ucontext_t context;

	enum ProcState state;
}PCB;

PCB* ProcessTable[MAX_PROC];
Pid_t PCBcnt;
PCB* curproc;
Mutex kernel_lock;

/*
 *
 * Concurrency control
 *
 */

#define QUANTUM (5000L)

struct itimerval quantum_itimer;

/*quantum_itimer.it_interval.tv_sec = 0L;
quantum_itimer.it_interval.tv_usec = QUANTUM;
quantum_itimer.it_value.tv_sec = 0L;
quantum_itimer..it_value.tv_usec = QUANTUM;
*/

void reset_timer()
{
	setitimer(ITIMER_VIRTUAL, &quantum_itimer, NULL);
}


void schedule(int sig){

}

void yield() {schedule(0);}

void run_scheduler()
{
	struct sigaction sa;
	int err;
	sa.sa_handler = schedule;
	sa.sa_flags = SA_RESTART;
	sigemptyset(&(sa.sa_mask));

	while((err = sigaction(SIGVTALRM, &sa, NULL)) && (errno==EINTR));
	assert(err==0);

	reset_timer();

	curproc = ProcessTable[0];
	//swapcontext(&_saved_context, &(curproc->context));
}

int Mutex_TryLock(Mutex *lock)
{
  char oldval;
  __asm__ __volatile__("xchgb %b0,%1"
		       :"=q" (oldval), "=m" (*lock)
		       :"0" (0) : "memory");
  return oldval > 0;
}

void Mutex_Unlock(Mutex* lock)
{
  *lock = MUTEX_INIT;
}

void Mutex_Lock(Mutex* lock)
{
	while(!Mutex_TryLock(lock))
		yield();
}

void Cond_Init(CondVar* cv)
{
  cv->waitset = NULL;
  cv->wstail = NULL;
}

static Mutex condvar_mutex = MUTEX_INIT;

void Cond_Wait(Mutex* mutex, CondVar* cv)
{

	tinyos_cv_waiter waitnode;
	waitnode.pid = GetPid();
	waitnode.next = NULL;

	Mutex_Lock(&condvar_mutex);

	if(cv->waitset==NULL)
		cv->wstail = &(cv->waitset);
	*(cv->wstail) = &waitnode;
	cv->wstail = &(waitnode.next);

	Mutex_Unlock(mutex);
	//release_and_sleep(STOP_CVWAIT, &condvar_mutex);

	Mutex_Lock(mutex);
}

static void doSignal(CondVar* cv)
{
	if(cv->waitset != NULL){
		tinyos_cv_waiter *node = cv->waitset;
		cv->waitset = node->next;
		//wakeup(node->pid,STOP_CVWAIT);
	}
}

void Cond_Signal(CondVar* cv)
{
	Mutex_Lock(&condvar_mutex);
	doSignal(cv);
	Mutex_Unlock(&condvar_mutex);
}

void Cond_Broadcast(CondVar* cv)
{
	Mutex_Lock(&condvar_mutex);
	while(cv->waitset != NULL)
		doSignal(cv);
	Mutex_Unlock(&condvar_mutex);
}

void init_context(ucontext_t* uc, void* stack, size_t stack_size, Task task, int argl, void* args)
{
	getcontext(uc);
	uc->uc_link = NULL;
	uc->uc_stack.ss_sp = stack;
	uc->uc_stack.ss_size = stack_size;
	uc->uc_stack.ss_flags = 0;
	makecontext(uc, task, 2, argl, args);
}

#define PROCESS_STACK_SIZE 65536

sigset_t scheduler_sigmask;
/*sigemptyset(&scheduler_sigmask);
sigaddset(&scheduler_sigmask, SIGVTALRM);*/

void pause_scheduling() {sigprocmask(SIG_BLOCK,&scheduler_sigmask, NULL);}
void resume_scheduling() {sigprocmask(SIG_UNBLOCK, &scheduler_sigmask, NULL);}

/*
 *
 * System calls
 *
 */


void Exit(int exitval)
{

}

Pid_t Exec(Task call, int argl, void* args)
{
	ucontext_t unew;//,uold;
	void* stack = malloc(PROCESS_STACK_SIZE);
	init_context(&unew, stack, PROCESS_STACK_SIZE, call, 2, 0);
	PCB* temp = malloc(sizeof(PCB));
	ProcessTable[PCBcnt] = temp;
	(ProcessTable[PCBcnt])->context = unew;
	(ProcessTable[PCBcnt])->parent_pid = (PCBcnt==0) ? 0 : curproc->pid;
	(ProcessTable[PCBcnt])->pid = PCBcnt;
	(ProcessTable[PCBcnt])->state = READY;
	curproc = ProcessTable[PCBcnt++];
	call(argl,args);
	printf("\n\n%d\n\n",curproc->pid);
	return curproc->pid;
}

Pid_t GetPid()
{
  return curproc->pid;
}


Pid_t WaitChild(Pid_t cpid, int* status)
{

	if((ProcessTable[--PCBcnt])->state == FINISHED)
		return ProcessTable[PCBcnt]->pid;
	return NOPROC;
}

/*
 *
 * Initialization
 *
 */

void boot(Task boot_task, int argl, void* args)
{
	PCBcnt = 0;
	Exec(boot_task,argl,args);
	sigemptyset(&scheduler_sigmask);
	sigaddset(&scheduler_sigmask, SIGVTALRM);
	quantum_itimer.it_interval.tv_sec = 0L;
	quantum_itimer.it_interval.tv_usec = QUANTUM;
	quantum_itimer.it_value.tv_sec = 0L;
	quantum_itimer.it_value.tv_usec = QUANTUM;
}

