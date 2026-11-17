/* LAB4 function3 2412412 */
#include <riscv.h>

/* switch_to - save the callee saved part of the running thread's context into
 * *from and let the target thread continue from *to.
 *
 *   a0 = from (struct context *), a1 = to (struct context *)
 *
 * struct context in kern/process/proc.h is just an array of uintptr_t:
 *
 *   0: ra   1: sp   2: s0   3: s1   ...   13: s11      (field * REGBYTES)
 *
 * Only the callee saved registers belong to a thread's context: the caller
 * saved ones are already dead at the call site.  A new thread does not return
 * here on its first run - copy_thread() stores forkret in context.ra, so the
 * "ret" below enters forkret -> forkrets -> __trapret.  switch_to therefore
 * touches nothing except the two context blocks.
 */
    .globl switch_to
    .text
    .align 2
switch_to:
    # save the current thread's context into *from
    STORE ra, 0*REGBYTES(a0)
    STORE sp, 1*REGBYTES(a0)
    STORE s0, 2*REGBYTES(a0)
    STORE s1, 3*REGBYTES(a0)
    STORE s2, 4*REGBYTES(a0)
    STORE s3, 5*REGBYTES(a0)
    STORE s4, 6*REGBYTES(a0)
    STORE s5, 7*REGBYTES(a0)
    STORE s6, 8*REGBYTES(a0)
    STORE s7, 9*REGBYTES(a0)
    STORE s8, 10*REGBYTES(a0)
    STORE s9, 11*REGBYTES(a0)
    STORE s10, 12*REGBYTES(a0)
    STORE s11, 13*REGBYTES(a0)

    # restore the target thread's context from *to
    LOAD ra, 0*REGBYTES(a1)
    LOAD sp, 1*REGBYTES(a1)
    LOAD s0, 2*REGBYTES(a1)
    LOAD s1, 3*REGBYTES(a1)
    LOAD s2, 4*REGBYTES(a1)
    LOAD s3, 5*REGBYTES(a1)
    LOAD s4, 6*REGBYTES(a1)
    LOAD s5, 7*REGBYTES(a1)
    LOAD s6, 8*REGBYTES(a1)
    LOAD s7, 9*REGBYTES(a1)
    LOAD s8, 10*REGBYTES(a1)
    LOAD s9, 11*REGBYTES(a1)
    LOAD s10, 12*REGBYTES(a1)
    LOAD s11, 13*REGBYTES(a1)

    ret
