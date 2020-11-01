#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

/* queue data structures */
struct proc* mlfq[5][NPROC+10];
int qpos[5] = { -1, -1, -1, -1, -1 };   // queue pointers
int qtmax[5] = { 1, 2, 4, 8, 16 };

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

/* queue interface functions */

// Returns 1 if process is found in queue
// Returns 0 otherwise

void
print_q_status()
{
  cprintf("queues:\n");
  for (int i = 0; i < 5; i++){
    cprintf("%d: ", i);
    for (int j = 0; j <= qpos[i]; j++){
      cprintf("%d ", mlfq[i][j]->pid);
    }
    cprintf("\n");
  }
}


int is_exists(int q, struct proc* el)
{
  struct proc* p;
  for (int i = 0; i <= qpos[q]; i++){
    p = mlfq[q][i];
    if (p->pid == el->pid)
      return 1;
  }
  return 0;
}

// Standard Queue ADT procedure
int
enqueue(int q, struct proc* el)
{
  // if (is_exists(q, el)){  // proc in queue
  //   return -1;
  // }
  
  // cprintf("attempt to enqueue pid %d\n", el->pid);
  for (int i = 0; i <= qpos[q]; i++){
    if (mlfq[q][i]->pid == el->pid)
      return -1;
  }
#ifdef MLD
  cprintf("success enqueue pid %d to q %d\n", el->pid, q);
#endif

  el->q_start = ticks;
  el->q = q;

  qpos[q]++;
  mlfq[q][qpos[q]] = el;

#ifdef MLD
  cprintf("q %d el %d\n", q, mlfq[q][qpos[q]]->pid);
  print_q_status();
#endif

  return 0;
}

int remove(int q, struct proc* el)
{
  int pos = -1;
  struct proc* p;
  for (int i = 0; i <= qpos[q]; i++){
    p = mlfq[q][i];
    if (p->pid == el->pid){
      pos = i;
      break;
    }
  }

  if (pos == -1){
    return -1;
  }

#ifdef MLD
  cprintf("Removing pid %d from q %d\n", el->pid, q);
#endif
  // restructure queue
  for (int i = pos; i < qpos[q]; i++){
    mlfq[q][i] = mlfq[q][i+1];
  }
  qpos[q]--;

  return 0;
}

int is_empty(int q){
  return qpos[q] >= 0;
}

struct proc* dequeue(int q){
  if (!is_empty(q)){
    struct proc* p = mlfq[q][0];
    remove(q, p);
    return p;
  }
  return 0;
}

/* end queue interface */

/* mlfq functions */
int move_q(int in, int de, struct proc* el)
{
  if (remove(in, el) < 0)
    return -1;
  if (enqueue(de, el) < 0)
    return -1;
  
  return 0;
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}


int
set_priority(int new_priority, int pid)
{
  if (new_priority < 0 || new_priority > 100)
    return -1;

  struct proc* p;
  int found = 0;
  int old_priority = -1;

  // Find the process with pid pid
  acquire(&ptable.lock);
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if (p->pid == pid){
      found = 1;
      break;
    }
  }
  
  if (found){
    old_priority = p->priority;
    p->priority = new_priority;
  }
  release(&ptable.lock);

  if (!found)
    return -1;

  if (new_priority < old_priority){
    yield();
  }

  return old_priority;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;


  p->ctime = ticks;  // Initialize start time
  p->rtime = 0;      // Default
  p->etime = 0;      // Default
  p->iotime = 0;

  p->n_run = 0;
  p->priority = -1;
  p->q = -1;
  for (int i = 0; i < 5; i++)
    p->time_in_q[i] = -1;

#ifdef PBS
    p->priority = 60;
#endif
#ifdef MLFQ
    p->q = 0;
    p->q_time = 0;
    p->q_start = 0;
    for (int i = 0; i < 5; i++)
      p->time_in_q[i] = 0;
#endif

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];


  #ifdef MLFQ
    cprintf("MLFQ\n");
  #else
  #ifdef RR
    cprintf("RR\n");
  #else
  #ifdef PBS
    cprintf("PBS\n");
  #else
  #ifdef FCFS
    cprintf("FCFS\n");
  #endif
  #endif
  #endif
  #endif

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

#ifdef MLFQ
  enqueue(0, p);
#endif

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

#ifdef MLFQ
  enqueue(0, np);
#endif

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  // cprintf("pid %d exiting wtime=%d\n", curproc->pid, curproc->iotime);

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;

  // Update exit time of proc
  curproc->etime = ticks;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);

#ifdef MLFQ
        remove(p->q, p);
#endif
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}


// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
waitx(int* wtime, int* rtime)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;

        /* update param fields */
        *rtime = p->rtime;
        *wtime = p->etime - p->rtime - p->ctime - p->iotime;
        *wtime = p->iotime;

        cprintf("etime %d, rtime %d, ctime %d, iotime %d\n",
          p->etime, p->rtime, p->ctime, p->iotime);

        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
#ifdef MLFQ
        remove(p->q, p);
#endif
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        p->ctime = 0;
        p->rtime = 0;
        p->etime = 0;
        release(&ptable.lock);

        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}



// void
// scheduler(void)
// {
//   struct proc *p;
//   struct cpu *c = mycpu();
//   c->proc = 0;
  
//   for(;;){
//     // Enable interrupts on this processor.
//     sti();

//     // Loop over process table looking for process to run.
//     acquire(&ptable.lock);
//     for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
//       if(p->state != RUNNABLE)
//         continue;

//       // Switch to chosen process.  It is the process's job
//       // to release ptable.lock and then reacquire it
//       // before jumping back to us.
//       c->proc = p;
//       switchuvm(p);
//       p->state = RUNNING;

//       swtch(&(c->scheduler), p->context);
//       switchkvm();

//       // Process is done running for now.
//       // It should have changed its p->state before coming back.
//       c->proc = 0;
//     }
//     release(&ptable.lock);

//   }
// }



//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
#ifndef MLFQ
  struct proc *p;
#endif
  struct cpu *c = mycpu();
  c->proc = 0;

  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);

#ifndef MLFQ
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

#ifdef FCFS

    struct proc* p2;
    p = 0;
    for (p2 = ptable.proc; p2 < &ptable.proc[NPROC]; p2++){
      if (p2->state != RUNNABLE)
        continue;
      if (p2->pid < 2)
        continue;
      if (p == 0){
        p = p2;
      } else {
        if (p2->ctime < p->ctime)
          p = p2;
      }
    }

    if (p == 0)
      p = ptable.proc;
    
    // cprintf("sched run %d, ctime=%d, iotime=%d\n", p->pid, p->ctime, p->iotime);
    
#endif
#ifdef PBS
    struct proc* p2;
    struct proc* next_proc = 0;
    int minp = 101;
    for (p2 = ptable.proc; p2 < &ptable.proc[NPROC]; p2++){
      if (p2->state != RUNNABLE)
        continue;
      if (p2->pid > 1){
        if (p2->priority < minp){
          minp = p2->priority;
          next_proc = p2;
        }
      }
    }

    // if (p->priority != minp)
    //   continue;
    if (next_proc == 0)
      p = ptable.proc;
    else
      p = next_proc;
#endif

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      p->n_run++;
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      // cprintf("picked pid %d, ctime %d\n", p->pid, p->ctime);

      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
#endif

// #define MLFQ
#ifdef MLFQ
    // age
    for (int q = 1; q < 5; q++){
      struct proc* el;
      for (int i = 0; i <= qpos[q]; i++){
        el = mlfq[q][i];
        if (ticks - el->q_start > 25){  // Promote
          cprintf("PROMOTE PID(%d) from %d to %d\n", el->pid, el->q, el->q-1);
          // move_q(el->q, el->q-1, el);
#ifdef PLOT
          cprintf("\nPLOT %d %d %d\n", el->pid, el->q, ticks);
#endif
          remove(el->q, el);
          enqueue(el->q-1, el);
        }
      }
    }
    // pick proc
    struct proc* p = 0;

    for (int q = 0; q < 5; q++){
      if (qpos[q] >= 0){
        p = mlfq[q][0];
        remove(q, p);
        break;
      }
    }
    // print_q_status();
    // exec
    if (p != 0 && p->state == RUNNABLE){
      // cprintf("Picked %d\n", p->pid);
      p->q_time++;
      p->n_run++;
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;
      // cprintf("picked pid %d, ctime %d\n", p->pid, p->ctime);

      swtch(&(c->scheduler), p->context);
      switchkvm();
      c->proc = 0;

      if (p != 0 && p->state == RUNNABLE){
        int currq = p->q;
        int newq = p->q;
        // demote if exceeded time slice
        if (p->q_time >= qtmax[currq] && p->q != 4){
// #ifdef PLOT
//           cprintf("\nPLOT %d %d %d\n", p->pid, p->q+1, ticks);
// #endif
          newq++;
          // cprintf("proc %d moved to queue %d\n", p->pid, newq);
        }
        p->q_time = 0;
        enqueue(newq, p);
      }
    }
#endif

    release(&ptable.lock);
  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan){
      p->state = RUNNABLE;
#ifdef MLFQ
      p->q_time = 0;
      enqueue(p->q, p);
#endif
    }
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING){
        p->state = RUNNABLE;
#ifdef MLFQ
        enqueue(p->q, p);
#endif
      }
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}


int
ps(void)
{

  struct proc *p;
  sti();
  int c_ticks = ticks;
  cprintf("pid\tPriority\tState\tr_time\tw_time\tn_run\tcur_q\tq0\tq1\tq2\tq3\tq4\n");

  acquire(&ptable.lock);
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if (p->state == UNUSED)
      continue;
    static char *states[] = {
      [UNUSED]    "unused",
      [EMBRYO]    "embryo",
      [SLEEPING]  "sleep ",
      [RUNNABLE]  "runble",
      [RUNNING]   "run   ",
      [ZOMBIE]    "zombie"
    };
    cprintf("%d\t%d\t\t%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n",
      p->pid, p->priority, states[p->state], p->rtime,
      c_ticks-p->rtime-p->ctime, p->n_run, p->q,
      p->time_in_q[0], p->time_in_q[1], p->time_in_q[2],
      p->time_in_q[3], p->time_in_q[4]);
  }
  release(&ptable.lock);

  return 0;
}

int
inc_iotime()
{
  struct proc* p;
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if (p->state == RUNNABLE)
      p->iotime++;
  }
  return 0;
}