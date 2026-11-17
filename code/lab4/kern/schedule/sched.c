#include <list.h>
#include <sync.h>
#include <proc.h>
#include <sched.h>
#include <assert.h>

void
wakeup_proc(struct proc_struct *proc) {
    assert(proc->state != PROC_ZOMBIE && proc->state != PROC_RUNNABLE);
    proc->state = PROC_RUNNABLE;
}

//LAB4 function3 2412412
/* schedule - pick the next runnable thread and hand the CPU over to it.
 *
 * The scan starts right after the current thread, so the threads get the CPU
 * in a round-robin way; the idle thread is not a real candidate, therefore the
 * scan starts at the head of proc_list when the idle thread is running.  If no
 * thread is runnable, idleproc is selected so that the CPU always has a valid
 * context to run.
 *
 * The selection and the switch must stay in the same critical section: an
 * interrupt arriving between "choose next" and proc_run() would run schedule()
 * again and change current/the page table, leaving the real execution context
 * inconsistent with current.  proc_run() keeps its own interrupt window closed
 * while it flips current, so nesting is harmless. */
void
schedule(void) {
    bool intr_flag;
    list_entry_t *le, *last;
    struct proc_struct *next = NULL;

    local_intr_save(intr_flag);
    {
        // The current thread gives up the CPU voluntarily, it does not need to
        // be rescheduled any more until somebody asks for it again.
        current->need_resched = 0;

        // Round-robin: start the scan after the current thread.  The idle
        // thread is not part of the round-robin ring, so start from the head
        // of proc_list when the idle thread is the one giving up the CPU.
        last = (current == idleproc) ? &proc_list : &(current->list_link);
        le = last;
        do {
            if ((le = list_next(le)) != &proc_list) {
                struct proc_struct *p = le2proc(le, list_link);
                if (p != idleproc && p->state == PROC_RUNNABLE) {
                    next = p;
                    break;
                }
            }
        } while (le != last);

        // No runnable thread at all: fall back to the idle thread, so that the
        // CPU always has a valid context to run.
        if (next == NULL) {
            next = idleproc;
        }

        // proc_run() performs the real switch; calling it with the current
        // thread would be pointless, so it is skipped in that case.
        if (next != current) {
            proc_run(next);
        }
    }
    local_intr_restore(intr_flag);
}


