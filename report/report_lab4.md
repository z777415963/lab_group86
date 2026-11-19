# 操作系统实验报告

## 实验基本信息

|项目|内容|
|-|-|
|**实验名称**|Lab 4: 虚拟内存页操作、进程创建与进程调度|
|**小组成员**|2412412-郑志昊、2413498-陈梓轩、2412528-赵甫文|
|**完成日期**|2026-11-19|

### 小组分工

|成员|负责的练习/模块|
|-|-|
|2413498-陈梓轩|功能模块一：虚拟内存页操作|
|2412528-赵甫文|功能模块二：进程创建及初始化|
|2412412-郑志昊|功能模块三：进程调度与上下文切换|

---

## 一、实验目的

本实验的主要目的是：

1. 理解 Sv39 多级页表的查找过程，掌握页表项建立、替换、删除时物理页引用计数和 TLB 的同步维护。
2. 掌握内核线程控制块的初始化、内核栈分配、PID 分配、进程链表登记和线程唤醒流程。
3. 理解 trapframe 与 context 的分工：trapframe 保存线程首次进入内核线程入口所需的完整处理器现场，context 保存线程切换所需的 callee-saved 寄存器。
4. 掌握简单轮转调度、空闲线程、Sv39 的 satp 切换，以及汇编上下文切换如何与 C 调度器衔接。
5. 练习使用 AI 提示词完成操作系统底层代码，并以工程结构、提交记录、测试脚本和实际执行结果为依据核验实现。

---

## 二、实验环境

|成员|AI 编程工具|底层模型|备注|
|-|-|-|-|
|2412412-郑志昊|[Cline]|[DeepSeek-V4-Flash]|[无]|
|2413498-陈梓轩|[Codex]|[ChatGPT-5.6-sol]|[无]|
|2412528-赵甫文|[Pi]|[ChatGPT-6-Luna]|[无]|

---

## 三、实验整体逻辑分析

### 3.1 本章节的逻辑主线

本章围绕“从页表映射维护到第一个内核线程运行”的执行链展开。页操作模块提供稳定的映射维护基础；进程模块把线程入口、参数和首次运行现场装入 PCB 与内核栈；调度模块再从可运行线程中选出目标，通过页表切换和寄存器上下文恢复把 CPU 交给它。

启动及调度链可以概括为：

```text
kern_init
  -> pmm_init
  -> pic_init / idt_init
  -> vmm_init
  -> proc_init
       -> alloc_proc(idleproc)
       -> kernel_thread(init_main, ...)
            -> do_fork
                 -> setup_kstack / copy_mm / copy_thread
                 -> PID、hash_list、proc_list 登记
                 -> wakeup_proc
  -> clock_init / intr_enable
  -> cpu_idle
       -> schedule
            -> proc_run
                 -> 写入 Sv39 satp / flush_tlb
                 -> switch_to
                      -> forkret -> forkrets -> __trapret
                      -> kernel_thread_entry -> init_main
```

虚拟内存页操作与进程切换在本实验中的连接点是地址转换状态：`page_insert`/`page_remove` 维护页表叶子映射，`proc_run` 在切换目标线程时安装该线程应使用的页表。当前 `copy_mm` 仍是内核线程共享启动地址空间的占位实现，因此线程运行时使用 `boot_pgdir_pa`。

### 3.2 功能的逐步实现

1. **先实现页映射维护**：用 `get_pte` 找到或创建目标线性地址的叶子页表项；更新映射时同步调整旧页和新页的引用计数，并通过 `tlb_invalidate` 清除旧翻译。
2. **再创建线程对象**：`alloc_proc` 初始化干净 PCB；`proc_init` 建立全局管理结构并创建 idleproc；`kernel_thread` 和 `do_fork` 为 initproc 准备 trapframe、内核栈、PID、链表节点和 runnable 状态。
3. **最后接通调度路径**：`cpu_idle` 检查重调度标记，`schedule` 选择 runnable 线程，`proc_run` 更新当前线程并切换 satp，`switch_to` 保存和恢复 context。新线程经 `forkret` 和 `forkrets` 回到 trapframe，再从 `kernel_thread_entry` 调用实际线程函数。

---

## 四、实验内容与实现

> 本章按三个功能模块和 Challenge 组织。代码检查以 `lab4` 分支的指定提交为依据；报告编写时没有运行构建、grade 或 QEMU，因此测试结果只列出脚本的检查项，不把预期输出写成已通过结果。

### 功能模块一：虚拟内存页操作（`kern/mm/pmm.c`）

**负责人：** [2413498-陈梓轩]

**对应提交：** `ff6bf360b922e071d096783e2c9a06e1fa2d48c9`

#### 模块功能描述

**需要实现的函数：**

```c
int page_insert(pde_t *pgdir, struct Page *page, uintptr_t la, uint32_t perm);
void page_remove(pde_t *pgdir, uintptr_t la);
```

本模块负责在 Sv39 页表中建立、替换和撤销叶子映射。`get_pte(pgdir, la, 1)` 沿 `PDX1`、`PDX0` 找到页表项，必要时分配中间级页表页；`page_insert` 再写入物理页号和权限位。`page_remove` 使用 `get_pte(..., 0)` 查找现有映射，因此不会为了删除而新建页表。

删除和替换映射时，`page_remove_pte` 先取得旧 PTE 对应的 `Page`，减少引用计数；引用数归零时释放物理页，然后清除 PTE 并失效该线性地址的 TLB 项。插入新页时先增加新页引用数，再移除旧映射，因此把同一物理页重新映射到同一地址时，引用数净变化为零。

**实现结果（关键片段）：**

```c
pte_t *ptep = get_pte(pgdir, la, 1);
if (ptep == NULL) {
    return -E_NO_MEM;
}

page_ref_inc(page);
if (*ptep & PTE_V) {
    page_remove_pte(pgdir, la, ptep);
}
*ptep = pte_create(page2ppn(page), perm);
tlb_invalidate(pgdir, la);
```

```c
pte_t *ptep = get_pte(pgdir, la, 0);
if (ptep != NULL) {
    page_remove_pte(pgdir, la, ptep);
}
```

|设计点|做法|若做错会怎样|
|-|-|-|
|重复映射同一页|先增加新引用，再移除旧引用|若先释放旧页，可能把仍要映射的页释放回空闲链表|
|替换到另一物理页|移除旧 PTE 并减少旧页引用，再安装新 PTE|覆盖旧项但不减引用会泄漏物理页|
|删除不存在的映射|`get_pte(..., 0)` 返回空时直接返回|若删除路径创建页表，会无故消耗内存|
|TLB 同步|写入或清除映射后调用 `tlb_invalidate`|页表内存已更新，CPU 仍可能使用旧地址翻译|

#### 最终提示词

以下提示词取自 `report/prompt/prompt_lab4-1.md` 的主提示词，并结合本模块接口和本次实验要求整理：

````text
你现在是我的操作系统实验助教。我们小组正在完成操作系统 Lab4，我负责“功能模块一”。请基于实验手册、仓库 lab4 分支的最新版代码以及仓库原有框架，帮助我完成并核验模块一。

首先阅读实验手册和相关代码，明确 page_insert、page_remove 的职责、依赖和调用关系。实现时保留原有函数签名和工程风格，使用 get_pte、pte_create、pte2page、page_ref_inc、page_ref_dec、free_page 与 tlb_invalidate 等已有接口，不改动队友负责的模块。

page_insert 要建立或替换指定线性地址的映射，维护新旧物理页的引用计数、权限位和 TLB；同页重映射时引用数应保持不变。若 get_pte 分配页表失败，返回 -E_NO_MEM，并保证失败路径没有遗留的半成品状态。page_remove 对无映射地址应安静返回，对有效映射清除 PTE、维护引用计数并失效 TLB。

请核对空指针、分配失败、重复映射、不同页替换、重复删除和资源释放等边界。根据仓库 Makefile 和评分工具给出测试命令，区分真实运行结果与理论预期；不要把未运行的测试写成通过。
````

#### 实现迭代过程

##### 提交实现与静态核对

**实现内容：** 提交中新增 `page_remove_pte`，并实现公开的映射建立和撤销接口。与测试代码的正常路径相符，插入、替换、同页重映射、权限更新及重复删除都有对应逻辑。

**静态检查发现：** `get_pte` 会依次创建多个中间级页表页。如果较高层分配成功、后续较低层分配失败，`get_pte` 返回 `NULL`，但已写入的上层 PTE 和已分配页没有回滚；`page_insert` 虽然返回 `-E_NO_MEM`，该分配失败路径仍可能留下空中间页表页。现有 grade 测试没有覆盖这种耗尽内存的分支。要完全满足“失败时系统状态干净”的要求，需要在 `get_pte` 中实现分配回滚，或拆分分配步骤由调用方回滚。

---

### 功能模块二：进程创建及初始化（`kern/process/proc.c`）

**负责人：** [2412528-赵甫文]

**对应提交：** `d64f0de3e68c06c0b9ced7c98ff076de1648889f`

#### 模块功能描述

**需要实现的函数：**

```c
static struct proc_struct *alloc_proc(void);
void proc_init(void);
int kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags);
int do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf);
static void copy_thread(struct proc_struct *proc, uintptr_t esp, struct trapframe *tf);
static int copy_mm(uint32_t clone_flags, struct proc_struct *proc);
```

`alloc_proc` 使用 `kmalloc` 取得 PCB 并清零字段；`proc_init` 初始化 `proc_list` 和 PID 哈希表，把当前启动栈登记为 PID 0 的 idleproc，再创建 PID 1 的 initproc。`kernel_thread` 将函数地址和参数放入临时 trapframe 的 `s0`、`s1`，并把首次执行地址设为 `kernel_thread_entry`。

`do_fork` 按顺序申请 PCB 和内核栈，调用 `copy_mm`、`copy_thread`，随后在关中断区域内分配 PID、登记哈希表和进程链表，最后通过 `wakeup_proc` 把新线程改为 `PROC_RUNNABLE`。若内核栈或地址空间准备失败，则释放已经取得的资源。当前 `copy_mm` 只断言当前线程未使用用户地址空间并返回成功，符合本实验仅创建共享内核地址空间线程的范围。

新线程首次切换时，`copy_thread` 将 trapframe 放在内核栈顶，并把 context 的 `ra` 设为 `forkret`、`sp` 设为 trapframe 地址。控制流由 `forkret` 调用 `forkrets(current->tf)`，经 `__trapret` 恢复现场，再由 `kernel_thread_entry` 调用 `s0` 所指函数并把 `s1` 作为参数。

**实现结果（关键片段）：**

```c
proc->tf = (struct trapframe *)(proc->kstack + KSTACKSIZE - sizeof(struct trapframe));
*(proc->tf) = *tf;
proc->tf->gpr.a0 = 0;
proc->context.ra = (uintptr_t)forkret;
proc->context.sp = (uintptr_t)proc->tf;
```

```c
local_intr_save(intr_flag);
proc->pid = get_pid();
hash_proc(proc);
list_add(&proc_list, &proc->list_link);
nr_process++;
local_intr_restore(intr_flag);
wakeup_proc(proc);
```

**状态变化：**

|阶段|状态/资源|说明|
|-|-|-|
|`alloc_proc`|`PROC_UNINIT`、PID -1|PCB 字段和链表节点已初始化|
|`setup_kstack`、`copy_thread`|内核栈和首次 trapframe/context|准备线程第一次被调度时的恢复现场|
|`do_fork` 登记|PID、hash_link、list_link、进程计数|线程正式进入进程管理结构|
|`wakeup_proc`|`PROC_RUNNABLE`|线程成为调度器候选|
|`proc_run` 首次切换|`forkret -> forkrets -> __trapret`|恢复 trapframe 后进入线程函数|

#### 最终提示词

以下为 `report/prompt/prompt_lab4-2.md` 中的实现提示词要点，按示例报告的格式整理：

````text
任务：修改 kern/process/proc.c，补全 alloc_proc、proc_init、kernel_thread、do_fork、copy_thread 和 copy_mm。保留已有结构与函数签名，并核对 proc.h、trap.h 的真实字段名称。

[RELY]
- PCB 包含 state、pid、runs、kstack、need_resched、parent、mm、context、tf、pgdir、flags、name、list_link 和 hash_link；trapframe 包含 gpr、status、epc、badvaddr 和 cause。
- 使用现有 kmalloc/kfree、setup_kstack/put_kstack、get_pid、hash_proc、wakeup_proc、list_add、local_intr_save/restore 等接口。
- 首次运行入口为 kernel_thread_entry；copy_thread 设置 context.ra 为 forkret，forkret 再调用 forkrets(current->tf)。

[GUARANTEE]
- alloc_proc 初始化干净 PCB；proc_init 初始化全局结构、idleproc 和 initproc。
- kernel_thread 构造临时 trapframe；do_fork 完成资源分配、上下文设置、链表登记和唤醒。
- copy_thread 负责将 trapframe 放到新线程内核栈顶；copy_mm 在本实验中维持内核地址空间共享的占位语义。
- 不修改公开函数签名；任何失败路径都要释放已分配的 PCB 和内核栈。

[SPECIFICATION]
- idleproc 使用 PID 0 和 bootstack，状态为 PROC_RUNNABLE；initproc 由 kernel_thread(init_main, "Hello world!!", 0) 创建，命名为 init，预期 PID 为 1。
- tf.gpr.s0 保存线程函数，tf.gpr.s1 保存参数；tf.epc 指向 kernel_thread_entry；status 设置 SPP、SPIE 并清除 SIE。
- copy_thread 将 tf 拷贝到 kstack 顶部，设置 a0=0、context.ra=forkret、context.sp=tf。
- do_fork 先准备资源，再在中断关闭的临界区登记 PID 和链表，最后 wakeup_proc；失败返回负错误码。
````

#### 实现迭代过程

##### 提交实现与结构核对

**实现内容：** 进程初始化、内核线程创建、PCB/内核栈准备和失败清理均集中在指定提交。代码实际使用 `struct proc_struct.pgdir`，其初始值为 `boot_pgdir_pa`；提示词中的 `cr3` 是概念性字段名，需以本工程 `proc.h` 为准。

**关键核对点：** `do_fork` 在链表挂接后才唤醒线程；copy_thread 建立的 context 与 `switch_to` 的寄存器布局配套；`copy_mm` 没有伪造用户地址空间复制。评分脚本检查 idleproc/initproc 的关键字段，以及 initproc 的首次运行输出。

---

### 功能模块三：进程调度与上下文切换（`kern/schedule/sched.c`、`kern/process/proc.c`、`kern/process/switch.S`）

**负责人：** [2412412-郑志昊]

**对应提交：** `3e36d86c524bea51f200aadc7b9e07e4882f6508`

#### 模块功能描述

**需要实现的函数：**

```c
void cpu_idle(void);
void schedule(void);
void proc_run(struct proc_struct *proc);
void switch_to(struct context *from, struct context *to);
```

`cpu_idle` 是 idleproc 的循环主体；检测到 `current->need_resched` 后调用 `schedule`。调度器在中断关闭期间清除当前线程的重调度标记，并从 `proc_list` 中轮转寻找 `PROC_RUNNABLE` 线程；idleproc 不作为普通轮转候选，没有其他 runnable 线程时作为回退目标。

`proc_run` 只负责执行切换动作：更新 `current`，按目标线程的地址空间构造 Sv39 satp 值，刷新 TLB，再调用 `switch_to`。本实验的内核线程 `mm == NULL`，因而使用 `boot_pgdir_pa`；代码也为后续拥有 `mm->pgdir` 的进程保留了物理地址转换路径。不得使用 `lsatp()`，因为该 helper 是 Sv32 旧接口。

`switch_to` 只保存和恢复 `struct context` 中的 `ra`、`sp`、`s0` 到 `s11`，然后 `ret`。它不决定调度策略。旧线程将从之前的 `switch_to` 调用点继续；新线程首次运行时则通过预置的 `context.ra = forkret` 进入 trapframe 恢复链。

**实现结果（关键片段）：**

```c
current = next;
uintptr_t pgdir_pa = boot_pgdir_pa;
if (next->mm != NULL && next->mm->pgdir != NULL) {
    pgdir_pa = PADDR(next->mm->pgdir);
}
write_csr(satp, ((uintptr_t)SATP_MODE_SV39 << 60) |
                 (pgdir_pa >> RISCV_PGSHIFT));
flush_tlb();
switch_to(&(prev->context), &(next->context));
```

|设计点|做法|若做错会怎样|
|-|-|-|
|调度与切换临界区|`schedule` 和 `proc_run` 使用 `local_intr_save/restore`|中断可能在 current、satp 和实际栈状态不一致时进入处理程序|
|Sv39 satp|mode 字段置为 8，并将页表物理地址转换为 PPN 放入低位|若把虚拟地址或 Sv32 格式写入 satp，地址转换会失效|
|context 内容|按 `struct context` 的 14 个 `uintptr_t` 槽存取 `ra`、`sp`、`s0`-`s11`|汇编偏移若与 C 结构体不一致，恢复出的寄存器和栈将错位|
|首次运行入口|新线程 `ra` 指向 `forkret`|若跳回普通调用点，新线程没有有效的调用现场|

#### 最终提示词

以下按 `report/prompt/prompt_lab4-3.md` 中的三个实现提示词归纳：

````text
提示词一：实现 schedule()
只在 sched.c 的 LAB4 function3 标记处实现 schedule。关中断保护调度选择，清除当前 need_resched，从当前链表位置之后轮转寻找 PROC_RUNNABLE 线程；idleproc 不参与普通候选，无可运行线程时回退到 idleproc。调度器只负责选择并调用 proc_run，不直接操作寄存器。

提示词二：实现 cpu_idle() 与 proc_run()
cpu_idle 持续循环，在需要重调度时调用 schedule。proc_run 对相同目标不做切换；否则在中断关闭期间更新 current，按 Sv39 格式写 satp、刷新 TLB，并调用 switch_to。内核线程使用 boot_pgdir_pa；如存在 mm->pgdir，则将其内核虚拟地址转换为物理地址。不要使用 Sv32 遗留的 lsatp()。

提示词三：实现 switch_to()
只保存和恢复 struct context 的 ra、sp、s0-s11，槽位顺序必须与 proc.h 一致。两个参数分别是 from 和 to；恢复完目标上下文后 ret。switch_to 不实现调度策略，也不修改 trapframe；新线程首次运行由 copy_thread 预置的 forkret 接续。
````

#### 实现迭代过程

##### 提交实现与控制流核对

**实现内容：** 指定提交新增 `sched.c` 和 `switch.S`，并补全 `proc.c` 中 `proc_run`、`cpu_idle`。`schedule` 采用轮转扫描；线程切换的 C 与汇编部分使用同一 `struct context` 布局。

**关键核对点：** idleproc 由 `proc_init` 设为需要调度，首次进入 `cpu_idle` 时会选中 initproc。`local_intr_save` 保存进入临界区前的 SIE 状态并清除 SIE；嵌套保存若发现 SIE 已关闭，会记录 false；恢复时仅在原先开启的情况下重新开启，因此不会把调用者原本关闭的中断意外打开。新线程的 SSTATUS.SPIE 在 trapframe 中置位，最终由 `sret` 按 RISC-V 语义恢复中断状态。

---

## 五、测试与验证

**测试机制说明：**

仓库 `code/lab4/Makefile` 提供 `make grade`。该目标先清理构建，再复制工程到 `/tmp/lab4_grade`，运行 `tools/inject.py` 注入 `tools/test_lab4.c`，最后由 `tools/grade.sh` 构建并运行 QEMU。评分脚本包含三组各 10 分的检查，总计 30 分。

|测试|注入位置|主要检查点|分值|
|-|-|-|-|
|PMM 映射|`pmm_init` 末尾|插入映射、PTE 内容、权限位、不同页替换、同页重映射引用数、重复删除及引用归零|10|
|进程创建|`proc_init` 末尾|idleproc 的 PID、状态、启动栈等字段；initproc 的 PID、状态、父子关系和 context|10|
|调度与首次运行|`init_main` 入口|两个测试线程都能被调度；输出 `this initproc, pid = 1, name = "init"`、参数字符串和测试 PASS 标记|10|

可在已配置 RISC-V 工具链和 QEMU 的环境中执行：

```sh
cd code/lab4
make clean
make
make grade
make qemu
```

`make qemu` 使用 `qemu-system-riscv64 -machine virt -nographic -bios default` 启动。退出 QEMU 可按 `Ctrl-A`，再按 `X`。

**本次实际核验结果：** 检查了指定提交的差异、三个模块的最终代码、`tools/test_lab4.c`、`tools/inject.py` 和 `tools/grade.sh`。本次没有运行编译、`make grade` 或 QEMU，因此没有实际测试通过记录，也没有可引用的运行截图。静态检查发现模块一的中间页表分配失败回滚风险，详见功能模块一的实现迭代过程；PMM 测试脚本未覆盖分配失败分支。

**测试截图：** 本次没有运行测试，故不附未验证的测试结果截图。

---

## 六、实验总结与收获

### 对操作系统的理解

**1. 本实验中的重要知识点与 OS 原理的对应关系**

|本实验的知识点|对应的 OS 原理知识点|二者的含义、关系与差异|
|-|-|-|
|PTE 与 `struct Page.ref`|虚拟内存映射和物理页生命周期|页表项描述虚拟地址到物理页的转换；引用计数描述物理页被多少映射持有。撤销最后一个映射后才可释放物理页|
|`get_pte` 与 Sv39 三层页表|多级页表的地址分解与逐级查找|Sv39 将虚拟地址拆成三个 9 位索引和 12 位页内偏移；每一级表项指向下一级页表，最后一级得到叶子 PTE|
|PCB、内核栈、trapframe、context|进程控制块与处理器上下文|PCB 保存管理状态；trapframe 保存完整 Trap 现场；context 只保存切换点必须保留的 callee-saved 寄存器，两者用途不同|
|`proc_list` 与 `schedule`|就绪队列和处理器调度|本实验用链表扫描实现简单轮转，不含优先级、时间片统计和多核负载均衡|
|`switch_to` 与 `forkret`|上下文切换与线程首次启动|已运行线程恢复到原调用点，新线程则通过人为构造的 context 进入 forkret，再恢复其 trapframe|
|`local_intr_save/restore`|临界区与本地中断屏蔽|单 hart 下暂时屏蔽本地中断，避免调度共享状态在更新中途被中断处理路径观察到；它不是多核锁|

**2. OS 原理中重要、但本实验尚未实现的内容**

* **用户地址空间复制与隔离**：`copy_mm` 当前只断言 `current->mm == NULL` 并返回成功，没有复制页表或建立独立用户空间。
* **完整进程回收**：`do_exit` 仍是 panic 占位实现；没有僵尸进程回收、父子等待或内核栈回收生命周期。
* **抢占式时间片调度**：实验实现了调度入口和轮转选择，但当前 timer handler 不驱动 `need_resched`，调度主要由 idle 入口和测试线程显式触发。
* **多核并发与锁**：`local_intr_save` 只屏蔽当前 hart 的中断，不能保护跨 hart 的链表或引用计数。
* **页表资源回收与失败原子性**：本次静态检查发现 `get_pte` 的后续层分配失败时没有释放先前分配的中间页表页；叶子映射没有写入，但分配器状态可能泄漏页面。

### Challenge 思考题

**1. `local_intr_save(intr_flag); ... local_intr_restore(intr_flag);` 如何开关中断？**

`__intr_save()` 读取 `sstatus.SIE`，若中断原本开启就调用 `intr_disable()` 并返回 1；原本关闭则返回 0。`__intr_restore(flag)` 仅在 flag 为 1 时重新调用 `intr_enable()`。因此嵌套临界区不会把原本关闭的中断误开。

**2. Sv32、Sv39、Sv48 有何异同，为什么 `get_pte()` 的两段代码相似？**

三种模式的共同点是都用多级页表逐层索引，非叶子 PTE 保存下一级页表物理页号和有效位，叶子 PTE 保存最终映射。Sv32 使用 32 位地址和两级 10 位索引；Sv39 使用 39 位规范虚拟地址和三级 9 位索引；Sv48 使用 48 位规范虚拟地址和四级 9 位索引。`get_pte()` 中相邻层级的工作相同：检查当前 PTE 是否有效，按需分配并清零下一级页表，再从 PTE 地址取得下一级页表。代码仅因级别索引宏不同而重复；增加分页层级本质上是增加同类的一次查找。

**3. `get_pte()` 将查找和分配合在一起是否合适？是否要拆分？**

`create` 参数让调用方可以选择只查找或允许分配，单一函数减少了遍历逻辑重复，适合当前简单实验。但合并后分配副作用藏在查找路径中，且多级分配中途失败需要回滚。更清晰的设计是将只读查找与逐级创建分开，或抽出单层遍历辅助函数并显式管理已分配页；若继续合并，则需完整处理部分失败的回滚。

### AI 协作开发的经验

1. **提示词要依赖真实工程结构。** `proc.h` 中字段为 `pgdir`，不是其他版本常见的 `cr3`；`switch_to` 的偏移由实际 `struct context` 决定；Sv39 satp 也不能用 Sv32 的 `lsatp()`。先读头文件与现有调用链，可以减少按熟悉版本猜接口造成的错误。
2. **失败路径与正常路径同样重要。** 页映射的普通插入、替换和删除逻辑清楚，但分层页表创建还要考虑后续层分配失败如何撤销先前资源。仅检查成功路径会遗漏这种资源泄漏。
3. **线程首次运行是一条跨语言恢复链。** C 代码构造 trapframe 和 context，汇编 `switch_to` 恢复 callee-saved 寄存器，`forkret`/`forkrets` 接上 trap 返回，最后 `kernel_thread_entry` 调用线程函数。任何一层的栈地址、寄存器偏移或入口地址不一致都会让新线程无法正确启动。
4. **要区分评分脚本覆盖与完整性保证。** `make grade` 的 PMM 检查覆盖了常见映射状态变化，但没有覆盖内存耗尽时的中间页表回滚。脚本通过只能证明列出的断言通过，不能替代边界路径审查。
5. **报告应区分静态检查、预期现象与实际测试。** 本次报告核对了提交代码和测试脚本，但没有运行构建或 QEMU，因此没有声称测试通过，也没有填写虚构的截图结果。

