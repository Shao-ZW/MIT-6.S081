#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);

  backtrace();

  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

uint64
sys_sigalarm(void)
{
  int ticks;
  uint64 handler;

  if(argint(0, &ticks) < 0)
    return -1;
  if(argaddr(1, &handler) < 0)
    return -1;
  
  myproc()->alarm_interval = ticks;
  myproc()->handler = handler;

  return 0;
}

uint64
sys_sigreturn(void)
{
  struct proc *p = myproc();
  struct trapframe *trapframe = p->trapframe;

  trapframe->epc = p->epc;
  trapframe->ra = p->ra;
  trapframe->sp = p->sp;
  trapframe->gp = p->gp;
  trapframe->tp = p->tp;
  trapframe->t0 = p->t0;
  trapframe->t1 = p->t1;
  trapframe->t2 = p->t2;
  trapframe->s0 = p->s0;
  trapframe->s1 = p->s1;
  trapframe->a0 = p->a0;
  trapframe->a1 = p->a1;
  trapframe->a2 = p->a2;
  trapframe->a3 = p->a3;
  trapframe->a4 = p->a4;
  trapframe->a5 = p->a5;
  trapframe->a6 = p->a6;
  trapframe->a7 = p->a7;
  trapframe->s2 = p->s2;
  trapframe->s3 = p->s3;
  trapframe->s4 = p->s4;
  trapframe->s5 = p->s5;
  trapframe->s6 = p->s6;
  trapframe->s7 = p->s7;
  trapframe->s8 = p->s8;
  trapframe->s9 = p->s9;
  trapframe->s10 = p->s10;
  trapframe->s11 = p->s11;
  trapframe->t3 = p->t3;
  trapframe->t4 = p->t4;
  trapframe->t5 = p->t5;
  trapframe->t6 = p->t6;

  return 0;
}