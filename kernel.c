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

typedef struct rlist {
	PCB* proc;
	struct rlist* prev;
	struct rlist* next;
}Read;

Read *head,*tail,*curproc;
PCB ProcessTable[MAX_PROC];
Pid_t PCBcnt;
Mutex kernel_lock = MUTEX_INIT;
ucontext_t kernel_context;

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

sigset_t scheduler_sigmask;
/*sigemptyset(&scheduler_sigmask);
sigaddset(&scheduler_sigmask, SIGVTALRM);*/

void pause_scheduling() {sigprocmask(SIG_BLOCK,&scheduler_sigmask, NULL);}
void resume_scheduling() {sigprocmask(SIG_UNBLOCK, &scheduler_sigmask, NULL);}

void schedule(int sig){
	PCB *old, *new;
	Read* i;
	//pause scheduling
	pause_scheduling();
	old = curproc->proc;
	new = head->proc;
	head = head->next;
	//printf("\nArg %d",i->proc->pid);
	//printf("\nold %d new %d curproc %d",old->pid,new->pid,curproc->proc->pid);
	if(old->state==FINISHED);
		//final_cleanup(old);

	reset_timer();
	//resume scheduling
	resume_scheduling();
	curproc->proc = new;
	//printf("\nold %d new %d curproc %d",old->pid,new->pid,curproc->pid);
	if(old!=new){
		//printf("\nold %d new %d curproc %d",old->pid,new->pid,curproc->proc->pid);
		swapcontext(&(old->context),&(new->context));
	}
}

void wakeup(Pid_t pid){
	ProcessTable[pid].state = READY;
}

void yield() {schedule(0);}

void release_and_sleep(Mutex* cv){
	//printf("\ncurp %d %d %d \n",curproc->proc->pid,head->proc->pid,tail->proc->pid);
	Mutex_Lock(cv);
	//printf("\ncurp %d %d %d \n",curproc->proc->pid,head->proc->pid,tail->proc->pid);
	curproc->proc->state = SLEEPING;
	printf("%d process is sleeping",curproc->proc->pid);
	//printf("\ncurp %d %d %d \n",curproc->proc->pid,head->proc->pid,tail->proc->pid);
	Mutex_Unlock(cv);
	yield();
}


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

	curproc = head;
	swapcontext(&kernel_context,&curproc->proc->context);
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
	printf("\nold  new \n");
	release_and_sleep(mutex);

	Mutex_Lock(mutex);
}

static void doSignal(CondVar* cv)
{
	if(cv->waitset != NULL){
		tinyos_cv_waiter *node = cv->waitset;
		cv->waitset = node->next;
		wakeup(node->pid);
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

void init_context(ucontext_t* uc, void* stack, size_t stack_size, Task call, int argl, void* args)
{
	getcontext(uc);
	uc->uc_link = NULL;
	uc->uc_stack.ss_sp = stack;
	uc->uc_stack.ss_size = stack_size;
	uc->uc_stack.ss_flags = 0;
	makecontext(uc, call, 2, argl, args);
}

#define PROCESS_STACK_SIZE 65536


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
	ucontext_t unew;
	void* stack = malloc(PROCESS_STACK_SIZE);
	Mutex_Lock(&kernel_lock);
	PCBcnt = PCBcnt+1;
	init_context(&unew, stack, PROCESS_STACK_SIZE, call, argl, args);
	ProcessTable[PCBcnt].context = unew;
	ProcessTable[PCBcnt].parent_pid = (curproc==NULL) ? 0 : curproc->proc->pid;
	ProcessTable[PCBcnt].pid = PCBcnt;
	ProcessTable[PCBcnt].state = READY;
	Read *temp = malloc(sizeof(Read*));
	temp->proc = &ProcessTable[PCBcnt];
	temp->next = NULL;
	temp->prev = NULL;
	if(head==NULL)
	{
		head = temp;
		head->next = head;
		head->prev = head;
	}
	else
	{
		temp->prev = head->prev;
		temp->prev->next = temp;
		head->prev = temp;
		temp->next = head;
	}
	Mutex_Unlock(&kernel_lock);
	return 1;//curproc->pid;
}

Pid_t GetPid()
{
  return curproc->proc->pid;
}


Pid_t WaitChild(Pid_t cpid, int* status)
{
	while(1)
		release_and_sleep(&condvar_mutex);
	return NOPROC;
	/*printf("Mpike!");
	if(ProcessTable[1].state == FINISHED)
		return ProcessTable[PCBcnt].pid;
	printf("Mpike!");
	return NOPROC;*/
}

/*
 *
 * Initialization
 *
 */

void boot(Task boot_task, int argl, void* args)
{
	PCBcnt = 0;
	curproc=NULL;
	head = NULL;
	tail = head;
	sigemptyset(&scheduler_sigmask);
	sigaddset(&scheduler_sigmask, SIGVTALRM);
	quantum_itimer.it_interval.tv_sec = 0L;
	quantum_itimer.it_interval.tv_usec = QUANTUM;
	quantum_itimer.it_value.tv_sec = 0L;
	quantum_itimer.it_value.tv_usec = QUANTUM;
	Exec(boot_task,argl,args);
	run_scheduler();
	printf("eftase");
}

