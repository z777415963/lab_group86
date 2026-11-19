\[PROMPT]

\*\*任务\*\*：修改 `kern/process/proc.c`，补全内核进程创建与初始化功能（`alloc\_proc`、`proc\_init`、`kernel\_thread`、`do\_fork`、`copy\_thread`、`copy\_mm`）。



\*\*操作要求\*\*：直接修改项目中的 `kern/process/proc.c`，保留现有代码与结构，仅在指定函数内补全逻辑。编码前请快速核对 `proc.h` 与 `trap.h` 中的结构体字段名称以确保一致。



\*\*输出要求\*\*：严格按照 `\[RELY]`、`\[GUARANTEE]`、`\[SPECIFICATION]` 完成代码修改，确保无警告编译通过，且能正常初始化 `idleproc` 并切换至 `initproc` 运行。



\[RELY]

\- \*\*数据结构与常量（`proc.h` / `trap.h`）\*\*：

&#x20; - `enum proc\_state`：`PROC\_UNINIT = 0`、`PROC\_SLEEPING`、`PROC\_RUNNABLE`、`PROC\_ZOMBIE`。

&#x20; - `struct proc\_struct`：`state`、`pid`、`runs`、`kstack`、`need\_resched`、`parent`、`mm`、`context`（含 `ra`, `sp`, `s0`\~`s11`）、`tf`、`cr3`、`flags`、`name\[PROC\_NAME\_LEN + 1]`、`list\_link`、`hash\_link`。

&#x20; - `struct trapframe`：`gpr`（含 `a0`, `sp`, `s0`, `s1` 等）、`status`、`epc`、`badvaddr`、`cause`。

&#x20; - 常量与宏：`KSTACKPAGE`、`KSTACKSIZE`、`MAX\_PROCESS`、`CLONE\_VM`、`E\_NO\_FREE\_PROC`、`SSTATUS\_SPP`、`SSTATUS\_SPIE`、`SSTATUS\_SIE`。

\- \*\*全局变量与汇编入口\*\*：

&#x20; - 全局变量：`proc\_list`、`hash\_list\[HASH\_LIST\_SIZE]`、`idleproc`、`initproc`、`current`、`nr\_process`、`boot\_pgdir\_pa`、`bootstack`。

&#x20; - 入口符号：`extern void kernel\_thread\_entry(void);`、`static void forkret(void);`（内部调用 `forkrets(current->tf)`）。

\- \*\*可调用工具函数\*\*：

&#x20; - 内存：`kmalloc`、`kfree`、`alloc\_pages`、`free\_pages`、`page2kva`、`kva2page`、`memset`、`memcpy`。

&#x20; - 进程与链表：`set\_proc\_name`、`find\_proc`、`get\_pid`、`wakeup\_proc`、`setup\_kstack`、`put\_kstack`、`hash\_proc`、`list\_init`、`list\_add`。

&#x20; - 中断与寄存器：`read\_csr(sstatus)`、`local\_intr\_save`、`local\_intr\_restore`。



\[GUARANTEE]

必须实现或补全以下 6 个函数（签名不可更改）：

1\. `static struct proc\_struct \*alloc\_proc(void);`：分配并初始化干净的 PCB。

2\. `void proc\_init(void);`：初始化全局链表，包装当前执行流为 `idleproc`，创建 `initproc`。

3\. `int kernel\_thread(int (\*fn)(void \*), void \*arg, uint32\_t clone\_flags);`：构造临时 `trapframe` 并调用 `do\_fork` 创建内核线程。

4\. `int do\_fork(uint32\_t clone\_flags, uintptr\_t stack, struct trapframe \*tf);`：分配资源、建立现场、挂载链表并唤醒新线程。

5\. `static void copy\_thread(struct proc\_struct \*proc, uintptr\_t esp, struct trapframe \*tf);`：在内核栈顶布置 `trapframe` 并设置首次调度 `context`。

6\. `static int copy\_mm(uint32\_t clone\_flags, struct proc\_struct \*proc);`：当前实验阶段的虚拟内存处理占位函数。



\*约束\*：允许新增内部辅助函数，但必须声明为 `static` 且仅限本文件使用。



\[SPECIFICATION]

1\. \*\*`alloc\_proc`\*\*

&#x20;  - \*\*Pre-Condition\*\*：`kmalloc` 已可用。

&#x20;  - \*\*Post-Condition\*\*：内存不足返回 `NULL`；成功则返回初始化完成的 `proc\_struct \*`（`state = PROC\_UNINIT`、`pid = -1`、`runs = 0`、`kstack = 0`、`need\_resched = 0`、`parent = NULL`、`mm = NULL`、`context` 清零、`tf = NULL`、`cr3 = boot\_pgdir\_pa`、`flags = 0`、`name` 清零）。



2\. \*\*`proc\_init`\*\*

&#x20;  - \*\*Pre-Condition\*\*：内存与中断子系统已初始化，当前运行在 `bootstack` 上。

&#x20;  - \*\*Post-Condition\*\*：`proc\_list` 与 `hash\_list` 初始化完成；`idleproc` 就绪（`pid = 0`、`state = PROC\_RUNNABLE`、`kstack = (uintptr\_t)bootstack`、`need\_resched = 1`、`name = "idle"`、`current == idleproc`）；通过 `kernel\_thread(init\_main, "Hello world!!", 0)` 创建 `initproc`（`pid = 1`、`name = "init"`）；`nr\_process == 2`（异常时触发 `panic`）。



3\. \*\*`kernel\_thread`\*\*

&#x20;  - \*\*Pre-Condition\*\*：`fn` 有效，全局进程结构已初始化。

&#x20;  - \*\*Post-Condition\*\*：在局部清零的 `struct trapframe tf` 中设置 `tf.gpr.s0 = (uintptr\_t)fn`、`tf.gpr.s1 = (uintptr\_t)arg`、`tf.status = (read\_csr(sstatus) | SSTATUS\_SPP | SSTATUS\_SPIE) \& \~SSTATUS\_SIE`、`tf.epc = (uintptr\_t)kernel\_thread\_entry`，返回 `do\_fork(clone\_flags | CLONE\_VM, 0, \&tf)`。



4\. \*\*`copy\_mm`\*\*

&#x20;  - \*\*Pre-Condition\*\*：`proc` 有效。

&#x20;  - \*\*Post-Condition\*\*：断言 `current->mm == NULL`，直接返回 `0`。



5\. \*\*`copy\_thread`\*\*

&#x20;  - \*\*Pre-Condition\*\*：`proc->kstack` 已分配，`tf` 有效。

&#x20;  - \*\*Post-Condition\*\*：`proc->tf = (struct trapframe \*)(proc->kstack + KSTACKSIZE - sizeof(struct trapframe))` 并拷贝 `\*tf`；设置 `proc->tf->gpr.a0 = 0`，若 `esp != 0` 则更新 `proc->tf->gpr.sp = esp`；设置 `proc->context.ra = (uintptr\_t)forkret`、`proc->context.sp = (uintptr\_t)(proc->tf)`。



6\. \*\*`do\_fork`\*\*

&#x20;  - \*\*Pre-Condition\*\*：`current != NULL`，`tf != NULL`。

&#x20;  - \*\*Post-Condition\*\*：成功返回新线程正数 `pid`；失败则完整回收已申请资源并返回负错误码（默认 `-E\_NO\_FREE\_PROC`）。

&#x20;  - \*\*Case 1（正常路径）\*\*：检查 `nr\_process < MAX\_PROCESS` -> `alloc\_proc()` 并设 `parent = current` -> `setup\_kstack(proc)`（或 `alloc\_pages(KSTACKPAGE)`） -> `copy\_mm` -> `copy\_thread` -> \*\*关中断临界区内\*\*分配 `get\_pid()`、加入 `hash\_list` 与 `proc\_list`、`nr\_process++` -> 恢复中断 -> 最后调用 `wakeup\_proc(proc)` 置为 `PROC\_RUNNABLE` -> 返回 `proc->pid`。

&#x20;  - \*\*Case 2（失败回滚）\*\*：`copy\_mm` 失败需先释放内核栈（`put\_kstack` / `free\_pages`）再释放 `proc`（`kfree`）；内核栈分配失败直接释放 `proc`；严禁未入链先唤醒或遗留悬空指针。

