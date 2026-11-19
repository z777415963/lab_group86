# Lab 4 提示词汇总

## 功能模块三：进程调度与上下文切换（kern/schedule/sched.c、kern/process/proc.c、kern/process/switch.S）

负责人：2412412-郑志昊

### 提示词 1：实现 schedule()（kern/schedule/sched.c，LAB4 function3）

[PROMPT]
**任务**：修改 `kern/schedule/sched.c` 文件，实现 `schedule()` 函数（LAB4 function3）。

**操作要求**：你必须在项目中修改实际文件，而不是仅展示代码。保留文件中现有代码与注释，只在 `//LAB4 function3 YOUR CODE` 标记位置补全 `schedule()` 实现；不要删除既有逻辑，不要改动函数签名，不要引入与本模块无关的功能。如需辅助函数，应设为 static 且只在本文件内部使用。

**输出要求**：直接进行文件操作，使用下面 [RELY]、[GUARANTEE]、[SPECIFICATION] 部分的信息。以工程真实代码为准，宏名、结构体字段名、函数名必须与工程一致，不要凭记忆改写。

[RELY]
// 关键数据结构与全局变量（以工程实际定义为准）
```c
struct proc_struct;        // 含 state、pid、need_resched、list_link 等字段
extern struct proc_struct *idleproc;
extern struct proc_struct *current;
extern list_entry_t proc_list;
extern int need_resched;
#define PROC_RUNNABLE
```

// 可用函数/符号
```c
local_intr_save(intr_flag); local_intr_restore(intr_flag);
list_next(le); le2proc(le, member);
void proc_run(struct proc_struct *proc);   // 在 proc.c 中实现，负责实际切换
```

[GUARANTEE]
必须实现或修改的接口：
```c
void schedule(void);
```

你可以根据需要添加：
- 辅助函数（建议设为 static）
- 模块内部使用的宏定义
- 模块内部的静态变量

[SPECIFICATION]
## schedule

**Pre-Condition**:
- 已存在 `proc_list` 进程链表，`current` 指向当前运行线程，`idleproc` 已创建并可用。
- 调用时可能处于中断上下文或普通内核上下文，调度选择过程需要保证不被中断打断。

**Post-Condition**:
- 已选出一个可运行线程并触发进程切换；若无可运行线程，则选择 `idleproc` 作为下一线程。
- 调度过程中对共享状态（`current`、`need_resched` 等）的访问保持原子性。

**Case 1**:
- 当 `proc_list` 中存在 state 为 `PROC_RUNNABLE` 的线程时，从链表中选出下一个可运行线程作为目标，更新相应重调度标记，然后调用 `proc_run` 切换到该线程。

**Case 2**:
- 当 `proc_list` 中不存在可运行线程时，选择 `idleproc` 作为目标线程并调用 `proc_run`，保证 CPU 不会无进程可运行。

**Requirements**:
- 调度选择与切换触发必须在关键区内完成，使用 `local_intr_save`/`local_intr_restore` 保护，避免当前线程、页表与实际执行现场不一致。
- `schedule` 只负责"选择 + 触发切换"，不直接操作寄存器上下文。


### 提示词 2：实现 cpu_idle() 与 proc_run()（kern/process/proc.c，LAB4 function3）

[PROMPT]
**任务**：修改 `kern/process/proc.c` 文件，实现 `cpu_idle()` 与 `proc_run()` 两个函数（LAB4 function3）。

**操作要求**：你必须在项目中修改实际文件，而不是仅展示代码。保留文件中现有代码与注释，只在 `//LAB4 function3 YOUR CODE` 标记位置补全实现；不要删除既有逻辑，不要改动函数签名，不要引入与本模块无关的功能。如需辅助函数，应设为 static 且只在本文件内部使用。

**输出要求**：直接进行文件操作，使用下面 [RELY]、[GUARANTEE]、[SPECIFICATION] 部分的信息。以工程真实代码为准，宏名、结构体字段名、函数名必须与工程一致，不要凭记忆改写。注意 `libs/riscv.h` 中的 `lsatp()` 是 Sv32 遗留，无法表达 Sv39 的 satp，本实验不可用；`kern/mm/pmm.c` 中的 `enable_paging()` 是正确写法参照。

[RELY]
// 关键数据结构与全局变量（以工程实际定义为准）
```c
struct proc_struct;        // 含 state、pid、kstack、tf、context、pgdir、need_resched、list_link 等字段
struct context;            // 含 ra、sp、s0~s11 等寄存器保存字段
extern struct proc_struct *idleproc;
extern struct proc_struct *current;
extern list_entry_t proc_list;
extern int need_resched;
#define SATP_MODE_SV39
#define RISCV_PGSHIFT
```

// 可用函数/符号
```c
read_csr(reg); write_csr(reg, val);
local_intr_save(intr_flag); local_intr_restore(intr_flag);
flush_tlb();
void schedule(void);                                  // 在 sched.c 中实现
void switch_to(struct context *from, struct context *to);  // 在 switch.s 中实现
boot_pgdir_pa;                                        // 内核页表物理地址
enable_paging() 的写法可作为 satp 切换的参照
```

// 注意：lsatp() 为 Sv32 遗留，不可用于本实验。

[GUARANTEE]
必须实现或修改的接口：
```c
void cpu_idle(void);
void proc_run(struct proc_struct *proc);
```

你可以根据需要添加：
- 辅助函数（建议设为 static）
- 模块内部使用的宏定义
- 模块内部的静态变量

[SPECIFICATION]
## cpu_idle

**Pre-Condition**:
- `idleproc` 已创建并成为 `current`，系统可能暂时没有其他可运行线程。

**Post-Condition**:
- 作为空闲线程的执行主体持续存在；当检测到需要重调度时调用 `schedule`，使 CPU 不空转丢失调度机会。

**Case 1**:
- 循环检测重调度需求（如 `need_resched` 等标记），一旦满足条件即调用 `schedule` 触发调度。

**Requirements**:
- `cpu_idle` 是 `idleproc` 的执行入口，必须持续循环，不应正常返回。
- 调度调用前需保证中断/关键区状态正确。

## proc_run

**Pre-Condition**:
- 参数 `proc` 是已创建且状态合法的目标线程；`current` 指向当前线程；目标线程的 `kstack`、`context`、`pgdir` 已初始化。

**Post-Condition**:
- `current` 已更新为目标线程；页表已切换到目标线程地址空间；执行现场已通过 `switch_to` 从旧线程迁移到新线程。

**Case 1**:
- 当目标线程与当前线程不同时，在关键区内更新 `current`，切换 satp/页表（参照 `enable_paging()` 的 Sv39 写法），刷新 TLB，然后调用 `switch_to(&from->context, &to->context)` 完成上下文切换。

**Case 2**:
- 当目标线程就是当前线程时，不做无意义的上下文切换，直接返回或保持现状。

**Requirements**:
- 整个切换过程必须保持原子性，使用 `local_intr_save`/`local_intr_restore` 保护。
- 页表切换必须使用 Sv39 正确的 satp 写法，不可使用 `lsatp()`。
- `proc_run` 不参与调度策略判断，只执行切换动作。


### 提示词 3：实现 switch_to()（kern/process/switch.S，LAB4 function3）

[PROMPT]
**任务**：修改 `kern/process/switch.S` 文件（工程中实际为大写 `.S`），实现 `switch_to()` 函数（LAB4 function3）。

**操作要求**：你必须在项目中修改实际文件，而不是仅展示代码。保留文件中现有代码与注释，只在 `//LAB4 function3 YOUR CODE` 标记位置补全 `switch_to` 汇编实现；不要删除既有逻辑，不要改动函数签名/标签名，不要引入与本模块无关的功能。

**输出要求**：直接进行文件操作，使用下面 [RELY]、[GUARANTEE]、[SPECIFICATION] 部分的信息。以工程真实代码为准，寄存器名、结构体字段偏移、标签名必须与工程一致，不要凭记忆改写。

[RELY]
// 关键数据结构（以工程实际定义为准）
```c
struct context;            // 含 ra、sp、s0~s11 等寄存器保存字段，字段顺序即内存偏移
```

// 相关符号
```c
switch_to(struct context *from, struct context *to);  // 本文件实现
forkret; forkrets; __trapret;                          // 新线程首次运行恢复链路，不得破坏
```

// 说明：switch_to 只做寄存器上下文保存与恢复，不参与调度策略判断。

[GUARANTEE]
必须实现或修改的接口：
```asm
void switch_to(struct context *from, struct context *to);
```

你可以根据需要添加：
- 本文件内部使用的标签
- 必要的 `.globl` / `.align` 等汇编指示

[SPECIFICATION]
## switch_to

**Pre-Condition**:
- `from` 与 `to` 分别指向旧线程和新线程的 `struct context`；调用发生在内核态，栈切换安全。
- `struct context` 的字段布局与汇编中使用的偏移严格一致。

**Post-Condition**:
- 旧线程的 callee-saved 寄存器已保存到 `from` 指向的 context；新线程的寄存器状态已从 `to` 指向的 context 恢复；执行流转到新线程。

**Case 1**:
- 保存 `from` 的 `ra`、`sp`、`s0~s11` 等上下文寄存器，从 `to` 恢复对应寄存器，返回后即在新线程上下文中继续执行。

**Requirements**:
- 只做寄存器上下文保存与恢复，不参与调度策略判断。
- 新线程首次运行依赖前序模块已将其 `context.ra` 与 trapframe 组织为 `forkret -> forkrets -> __trapret` 恢复链路，`switch_to` 不得破坏该链路。
- 汇编实现需与 `struct context` 字段布局严格一致。



