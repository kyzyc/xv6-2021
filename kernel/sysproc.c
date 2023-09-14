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

  backtrace();

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

uint64 sys_sigalarm(void)
{
  int ticks;
  uint64 addr;

  if(argint(0, &ticks) < 0) {
    return -1;
  }
  if(argaddr(1, &addr) < 0) {
    return -1;
  }

  struct proc* p = myproc();

  p->ticks = ticks;
  p->handler = addr;

  return 0;
  
}

uint64 sys_sigreturn(void)
{
  struct proc* p = myproc();
  // printf("%p\n", p->trap_pc);
  p->trapframe->epc = p->trap_pc;

  p->trapframe->a0 = p->trap_a0;
  p->trapframe->a1 = p->trap_a1;
  p->trapframe->a2 = p->trap_a2;
  p->trapframe->a3 = p->trap_a3;
  p->trapframe->a4 = p->trap_a4;
  p->trapframe->a5 = p->trap_a5;
  p->trapframe->a6 = p->trap_a6;
  p->trapframe->a7 = p->trap_a7;

  p->trapframe->t0 = p->trap_t0;
  p->trapframe->t1 = p->trap_t1;
  p->trapframe->t2 = p->trap_t2;
  p->trapframe->t3 = p->trap_t3;
  p->trapframe->t4 = p->trap_t4;
  p->trapframe->t5 = p->trap_t5;
  p->trapframe->t6 = p->trap_t6;
  
  p->trapframe->ra = p->trap_ra;
  p->trapframe->sp = p->trap_sp;

  p->trapframe->s0 = p->trap_s0;
  p->trapframe->s1 = p->trap_s1;
  //printf("%p\n", p->trap_pc);
  p->is_on_trap = 0;
  return 0;
}
