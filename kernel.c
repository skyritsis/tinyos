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



/*
 *
 * Concurrency control
 *
 */

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
  
}

void Cond_Init(CondVar* cv)
{
  cv->waitset = NULL;
  cv->wstail = NULL;
}

void Cond_Wait(Mutex* mx, CondVar* cv)
{

}

void Cond_Signal(CondVar* cv)
{

}

void Cond_Broadcast(CondVar* cv)
{

}


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
  return NOPROC;
}

Pid_t GetPid()
{
  return NOPROC;
}


Pid_t WaitChild(Pid_t cpid, int* status)
{
  return NOPROC;
}

/*
 *
 * Initialization
 *
 */

void boot(Task boot_task, int argl, void* args)
{

}

