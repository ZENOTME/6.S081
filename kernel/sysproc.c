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
  struct proc *p = myproc();
  	//Start
	//printf("Start\n");
	//vmprint_userspace(p->pagetable,1);
	//vmprint_userspace(p->k_pagetable,1);
  if(argint(0, &n) < 0)
    return -1;
  addr = p->sz;

  //if(n==-77824){
  //s  vmprint_userspace(p->pagetable,1);
  //}

  if(growproc(n) < 0)
    return -1;
  
  //if(n==-77824){
  //s  vmprint_userspace(p->pagetable,1);
  //}
	//printf("Grow n=%d old_sz=%d new_sz=%d\n",n,addr,p->sz);
	//vmprint_userspace(p->pagetable,1);
  // Map user memory to kernel_pagetable
  //if(n==-77824){
  //  vmprint_userspace(p->k_pagetable,1);
  //}
  if(p->sz>PLIC||uvmkcopy(p->pagetable, p->k_pagetable,addr,n) < 0){
    release(&p->lock);
    return -1;
  }
  //if(n==-77824){
  //  vmprint_userspace(p->k_pagetable,1);
  //}
  	//End
	//printf("End\n");
	//vmprint_userspace(p->pagetable,1);
	//vmprint_userspace(p->k_pagetable,1);
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
