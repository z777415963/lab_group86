# Lab 3 提示词汇总

## 功能模块一：Trap 上下文恢复与 Trap 入口（kern/trap/trapentry.S）

负责人：2412412-郑志昊

### 提示词 1：实现 RESTORE_ALL 宏

[PROMPT]
**任务**：修改文件 `kern/trap/trapentry.S`，实现 `RESTORE_ALL` 宏。该宏负责把当前内核栈上 Trap frame 中保存的处理器状态恢复到寄存器与 CSR 中，为随后的 `sret` 返回原执行流做好准备。本任务只处理 `RESTORE_ALL` 宏本体，不涉及 `__alltraps` 的调用衔接（那属于另一项任务）。

**操作要求**：你必须在项目中修改实际文件，而不是仅展示代码片段。保留文件中现有全部代码，尤其是已提供的 `SAVE_ALL` 宏、`__alltraps`、`__trapret` 标签和 `sret` 指令，不得删除或改写它们；只在 `RESTORE_ALL` 宏内 `lab3 function1 YOUR CODE` 标记处补全实现。不要修改 `SAVE_ALL`、`__alltraps`、`__trapret` 的任何一行。

**输出要求**：直接进行文件操作，使用下面 [RELY]、[GUARANTEE]、[SPECIFICATION] 提供的信息，以工程真实代码为准。`RESTORE_ALL` 必须与已提供的 `SAVE_ALL` 严格对称，不得自行假设与工程不一致的 Trap frame 布局。不要引入与本宏无关的功能。

[RELY]
// 宏定义（来自 libs/riscv.h，工程真实定义）
#if __riscv_xlen == 64
#define STORE    sd
#define LOAD     ld
#define LOG_REGBYTES 3
#endif
#define REGBYTES (1 << LOG_REGBYTES)   // riscv64 下 REGBYTES = 8

// 数据结构（来自 kern/trap/trap.h，工程真实定义）
struct pushregs {
    uintptr_t zero, ra, sp, gp, tp, t0, t1, t2, s0, s1,
              a0, a1, a2, a3, a4, a5, a6, a7,
              s2, s3, s4, s5, s6, s7, s8, s9, s10, s11,
              t3, t4, t5, t6;   // 共 32 个 uintptr_t，顺序即 x0~x31
};
struct trapframe {
    struct pushregs gpr;   // 偏移 0~31
    uintptr_t status;      // 偏移 32
    uintptr_t epc;         // 偏移 33
    uintptr_t badvaddr;    // 偏移 34
    uintptr_t cause;       // 偏移 35
};

// Trap frame 与 SAVE_ALL 槽位的对应关系（RESTORE_ALL 必须严格镜像）
// 偏移（单位 REGBYTES）：
//   0~31  -> gpr.zero ~ gpr.t6（其中 gpr.sp 位于偏移 2）
//   32    -> status   （对应 CSR sstatus）
//   33    -> epc      （对应 CSR sepc）
//   34    -> badvaddr （对应 CSR sbadaddr/stval，riscv.h 中 CSR_SBADADDR 0x143）
//   35    -> cause    （对应 CSR scause）
// 注意：SAVE_ALL 中 x2(sp) 的值来自 sscratch，并非普通 STORE x2 保存；
//       偏移 2 处存的是 Trap 发生时的原 sp。

// 可用 CSR 指令（riscv.h 提供 CSR 名：sstatus/sepc/scause/sbadaddr）
//   csrr rd, <csr>   读 CSR
//   csrw <csr>, rs   写 CSR
// 已提供的 sret 指令由 __trapret 在 RESTORE_ALL 之后执行。
// 当前 sp（SAVE_ALL 之后）指向 Trap frame 起始地址。

[GUARANTEE]
必须实现或修改的接口：
- 汇编宏 RESTORE_ALL（位于 kern/trap/trapentry.S）
  其调用约定：进入时 sp 指向 Trap frame 起始；执行完毕时所有通用寄存器与 4 个 CSR 恢复为 Trap 发生时的值，sp 回到 Trap frame 之前的栈位置，随后可由 __trapret 执行 sret。

不得修改：
- SAVE_ALL 宏
- __alltraps、__trapret 标签及其内部既有代码
- struct trapframe 定义
- sret 指令

你可以根据需要添加：
- 宏内部临时使用的寄存器约定（不得破坏最终需恢复的寄存器值）
- 模块内部宏定义（如仅在本宏内使用的偏移常量）

[SPECIFICATION]
#### RESTORE_ALL

**Pre-Condition**:
- 当前处于 S 模式 Trap 处理流程中。
- sp 指向一个由 SAVE_ALL 按上述布局填好的 Trap frame 起始地址。
- Trap frame 中 36 个槽位均已被 SAVE_ALL 写入有效值。

**Post-Condition**:
- 4 个 CSR（sstatus、sepc、sbadaddr、scause）已从偏移 32~35 恢复。
- 通用寄存器 x1、x3~x31 已从偏移 1、3~31 恢复。
- x0 恒为 0，无需从内存恢复。
- sp 已回退到 Trap frame 分配之前的位置（即执行 SAVE_ALL 中 `addi sp, sp, -36*REGBYTES` 之前的 sp）。
- 宏执行完毕后，处理器状态等价于 Trap 发生瞬间的状态（CSR 与通用寄存器一致），可安全执行 sret 返回。

**Case 1**（正常恢复路径）:
- 触发条件：sp 指向合法 Trap frame。
- 执行行为：按偏移从 Trap frame 读取 sstatus、sepc、sbadaddr、scause 并写回对应 CSR；读取 x1、x3~x31 并写回；随后将 sp 调整回 Trap frame 之前的位置。
- 最终状态：满足上述 Post-Condition，控制流交回 __trapret 执行 sret。

**Requirements**:
- 必须与 SAVE_ALL 严格对称：偏移、槽位、寄存器一一对应，不得新增或遗漏槽位。
- 对 x2(sp) 的处理必须与 SAVE_ALL 的特殊处理相呼应：SAVE_ALL 中 x2 的值来自 sscratch 而非普通保存，因此 RESTORE_ALL 不应把偏移 2 处的内容当作普通寄存器简单 load 回 sp；sp 的恢复应通过调整栈指针回到 Trap frame 之前的位置来实现，保证与 SAVE_ALL 的 addi 对称。
- 恢复 CSR 与通用寄存器的先后顺序、以及临时寄存器的使用，不得导致最终恢复值被自身覆盖。
- 不得破坏 __trapret 后续执行 sret 所需的状态。


### 提示词 2：补全 __alltraps 的调用衔接

[PROMPT]
**任务**：修改文件 `kern/trap/trapentry.S`，补全 `__alltraps` 标签中 `SAVE_ALL` 之后、`__trapret` 之前的衔接代码。该代码负责把 `SAVE_ALL` 建立的 Trap frame 地址按 RISC-V 调用约定作为参数传给 C 语言函数 `void trap(struct trapframe *tf)`，并在 `trap()` 返回后让控制流进入 `__trapret` 完成上下文恢复与 `sret` 返回。本任务只处理 `__alltraps` 的调用衔接，不实现 `RESTORE_ALL` 宏本体（那属于另一项任务）。

**操作要求**：你必须在项目中修改实际文件，而不是仅展示代码片段。保留文件中现有全部代码，包括 `SAVE_ALL` 宏、`RESTORE_ALL` 宏、`__alltraps` 中的 `SAVE_ALL` 调用、`__trapret` 标签与 `sret`，不得删除或改写它们；只在 `__alltraps` 中 `lab3 function1 YOUR CODE` 标记处补全实现。不要修改 `SAVE_ALL`、`RESTORE_ALL`、`__trapret`、`struct trapframe` 和 `trap()` 的函数签名。

**输出要求**：直接进行文件操作，使用下面 [RELY]、[GUARANTEE]、[SPECIFICATION] 提供的信息，以工程真实代码为准。参数传递必须符合 RISC-V 调用约定，且不得破坏 `SAVE_ALL` 建立的 Trap frame 或后续 `RESTORE_ALL` 所需的信息。不要引入与本衔接无关的功能。

[RELY]
// 宏定义（来自 libs/riscv.h，工程真实定义）
#if __riscv_xlen == 64
#define STORE    sd
#define LOAD     ld
#define LOG_REGBYTES 3
#endif
#define REGBYTES (1 << LOG_REGBYTES)   // riscv64 下 REGBYTES = 8

// 数据结构（来自 kern/trap/trap.h，工程真实定义）
struct pushregs {
    uintptr_t zero, ra, sp, gp, tp, t0, t1, t2, s0, s1,
              a0, a1, a2, a3, a4, a5, a6, a7,
              s2, s3, s4, s5, s6, s7, s8, s9, s10, s11,
              t3, t4, t5, t6;   // 共 32 个 uintptr_t
};
struct trapframe {
    struct pushregs gpr;   // 偏移 0~31
    uintptr_t status;      // 偏移 32
    uintptr_t epc;         // 偏移 33
    uintptr_t badvaddr;    // 偏移 34
    uintptr_t cause;       // 偏移 35
};
// SAVE_ALL 执行后 sp 指向的地址即为 struct trapframe 起始地址，
// 可直接作为参数传给 trap()。

// 可用函数 / 标签（来自 trap.h 与 trapentry.S）
void trap(struct trapframe *tf);   // C 语言 Trap 处理接口，参数按 RISC-V 调用约定放 a0
// 已提供的 SAVE_ALL：执行后 sp 指向新建 Trap frame 起始，Trap frame 已完整保存
// 已提供的 __trapret 标签：执行 RESTORE_ALL 后 sret
// RISC-V 调用约定：第一个整型/指针参数通过 a0 传递；jal 带返回地址跳转。

[GUARANTEE]
必须实现或修改的接口：
- __alltraps 标签中 SAVE_ALL 之后到 __trapret 之前的衔接代码（位于 kern/trap/trapentry.S）
  其调用约定：进入该段时 sp 指向 SAVE_ALL 建立的 Trap frame 起始；执行后应完成 trap(tf) 调用，trap() 返回后控制流进入 __trapret。

不得修改：
- SAVE_ALL 宏、RESTORE_ALL 宏
- __alltraps 中的 SAVE_ALL 调用、__trapret 标签及 sret
- struct trapframe 定义
- void trap(struct trapframe *tf) 的函数签名

你可以根据需要添加：
- 该段内部临时使用的寄存器（需保证不破坏 Trap frame 与恢复所需信息）
- 模块内部宏定义

[SPECIFICATION]
#### __alltraps 调用衔接段

**Pre-Condition**:
- 已执行 SAVE_ALL，当前 sp 指向完整的 Trap frame 起始地址，Trap frame 内 36 个槽位均已有效。
- 尚处于汇编 Trap 入口，未调用 C 处理函数。

**Post-Condition**:
- trap() 已被调用，且其参数 tf 等于 SAVE_ALL 后 sp 所指的 Trap frame 起始地址。
- trap() 返回后，控制流进入 __trapret，此时 sp 仍指向该 Trap frame 起始地址（与调用 trap 前一致），Trap frame 内容未被破坏，RESTORE_ALL 可据此正确恢复。
- 整个衔接过程不改变 Trap frame 中任何槽位的值。

**Case 1**（正常调用路径）:
- 触发条件：SAVE_ALL 已正常完成。
- 执行行为：按 RISC-V 调用约定把当前 Trap frame 起始地址作为第一个参数放入 a0；跳转调用 trap()；trap() 返回后控制流落到 __trapret。
- 最终状态：满足上述 Post-Condition，进入 __trapret。

**Requirements**:
- 必须保证传给 trap() 的指针就是 SAVE_ALL 后 sp 所指的 Trap frame 起始地址，不得偏移或另建副本。
- 调用 trap() 前后 sp 必须保持一致（与 "sp should be the same as before 'jal trap'" 的注释要求相符），不得在调用前额外压栈或调用后额外弹栈而改变 Trap frame 位置。
- 使用 jal（或等效带返回地址的跳转）调用 trap，使 trap() 返回后能回到 __trapret 衔接处；不得使用会破坏返回路径的跳转方式。
- 不得破坏 a0 之外 Trap frame 中保存的任何寄存器值（恢复由 RESTORE_ALL 负责）。
- 不得修改 trap() 签名，不得在汇编中直接实现 trap 的逻辑。


### 提示词 3：统一 CSR 助记符 sbadaddr → stval

[PROMPT]
**任务**：`sbadaddr` 是 RISC-V 特权规范的旧名称，新版工具链已将其更名为 `stval`，请把 `kern/trap/trapentry.S` 中所有 `sbadaddr` 替换为 `stval`。

**操作要求**：直接修改实际文件；`SAVE_ALL` 与 `RESTORE_ALL` 中的两处必须同时替换，保持入口与出口的对称性；注释中的名称一并同步。不得改动 CSR 编号（0x143）、Trap frame 偏移、C 结构体字段名 `badvaddr` 以及任何指令、寄存器、偏移。

**输出要求**：替换完成后确认文件中 `sbadaddr` 残留为 0，并说明 `libs/riscv.h` 中的 `CSR_SBADADDR` / `DECLARE_CSR(sbadaddr, ...)` 属 C 侧宏定义，不在汇编器解析范围内，故不改动。

[RELY]
// libs/riscv.h 中相关定义
#define CSR_SBADADDR 0x143
// 该行等价于旧规范中的 sbadaddr，新规范称为 stval
// 汇编器（gas）使用自身的 CSR 名称表解析裸 CSR 名，sstatus/sepc/scause 等均如此。

[GUARANTEE]
必须修改：`kern/trap/trapentry.S` 中全部 `sbadaddr` 出现位置（`SAVE_ALL` 的 `csrr s3, sbadaddr`、`RESTORE_ALL` 的 `csrw sbadaddr, s3`、以及相关注释）。
不得修改：`SAVE_ALL`/`RESTORE_ALL` 的指令结构、偏移、寄存器、`struct trapframe`、`libs/riscv.h`。

[SPECIFICATION]
**Pre-Condition**：文件中存在 3 处 `sbadaddr`（2 处指令 + 1 处注释），CSR 编号均为 0x143。
**Post-Condition**：全部替换为 `stval`，`sbadaddr` 残留为 0；行为与替换前完全等价，Trap frame 偏移 34 的语义不变。
**Requirements**：入口（保存）与出口（恢复）必须使用同一名称，避免出现"入口旧名、出口新名"的不对称写法。


## 功能模块二：中断与异常处理（kern/trap/trap.c）

负责人：2412528-赵甫文

### 提示词 1：实现 interrupt_handler 与 exception_handler

[PROMPT]
**任务**：修改 `kern/trap/trap.c`，实现中断与异常处理功能。

**操作要求**：在 `//lab3 function2 YOUR CODE` 标记处补全实现，保留现有代码，不修改函数签名、`trap_dispatch()`、`trap()` 或其他函数。处理函数返回后必须衔接现有 Trap 返回流程。

**输出要求**：直接修改项目文件，并确保实现符合以下 [RELY]、[GUARANTEE] 和 [SPECIFICATION]。完成后尝试使用 `make qemu` 验证；若构建或启动失败，检查并修正本任务涉及的代码及 Trap 入口字段兼容问题。

[RELY]
- `kern/trap/trap.h` 定义了 `struct trapframe`，包含 `uintptr_t epc` 和 `uintptr_t cause`。
- `kern/trap/trap.c` 已定义 `#define TICK_NUM 100`，并提供 `print_ticks()`；该函数输出 `100 ticks`，在 `DEBUG_GRADE` 下还会输出 `End of Test.` 并调用 `panic()`。
- `trap_dispatch(tf)` 已按 `tf->cause` 的最高位区分中断与异常，并分别调用两个处理函数；`trap()` 返回后由 `kern/trap/trapentry.S` 恢复 trapframe 并执行 `sret`。
- `kern/driver/clock.h` 声明 `extern volatile size_t ticks;` 和 `void clock_set_next_event(void);`。`kern/driver/clock.c` 中 `clock_set_next_event()` 会安排下一次时钟事件。
- `libs/riscv.h` 定义 `IRQ_S_TIMER`（值为 5）、`CAUSE_ILLEGAL_INSTRUCTION`（值为 2）、`CAUSE_BREAKPOINT`（值为 3），以及其他异常原因宏。
- `libs/sbi.h` 声明关机接口 `void sbi_shutdown(void);`。
- 当前工程没有定义 `SHUTDOWN_AFTER`。参考 `tools/grade.sh` 的预期（要求出现 `100 ticks` 和 `ticks=200`），在 `trap.c` 中以局部宏或常量明确关机阈值，达到累计 200 ticks 时关机。
- 兼容性注意：`kern/trap/trapentry.S` 使用 CSR 名 `stval` 保存 Trap 的异常值；`libs/riscv.h` 仍保留旧名 `sbadaddr`。不要为解决该名称差异而改动其他文件或新增不必要的 CSR 访问。

[GUARANTEE]
必须实现以下函数，签名保持不变：

```c
void interrupt_handler(struct trapframe *tf);
void exception_handler(struct trapframe *tf);
```

允许新增仅供 `trap.c` 内部使用的 `static` 辅助函数或局部常量；不得新增公开接口，也不得修改上述范围以外的函数。

[SPECIFICATION]
**`interrupt_handler(tf)`**

- 前置条件：`tf` 指向当前 Trap frame；`tf->cause` 表示中断。
- 对 supervisor timer interrupt（`IRQ_S_TIMER`），安排下一次时钟事件，每次中断将 `ticks` 增加一次。每当 `ticks` 达到 `TICK_NUM` 的整数倍，按要求输出统计信息；首次统计应包含现有 `print_ticks()` 的 `100 ticks` 输出，累计达到 200 ticks 时还应输出 `ticks=200` 并调用 `sbi_shutdown()`。
- 对其他可识别中断，输出对应类型提示；未识别中断调用 `print_trapframe(tf)`。
- 不修改 `tf->epc`，确保返回现有 Trap 返回流程。

**`exception_handler(tf)`**

- 前置条件：`tf` 指向当前 Trap frame；`tf->cause` 表示异常。
- 对 `CAUSE_ILLEGAL_INSTRUCTION`，输出以 `Illegal instruction at` 开头的提示，并在处理后推进 `tf->epc`，避免返回时重复执行该指令。
- 对 `CAUSE_BREAKPOINT`，输出以 `Breakpoint at` 开头的提示，并在处理后推进 `tf->epc`，避免返回时重复触发断点。
- 推进 `epc` 时依据 RISC-V 指令编码判断指令长度，不要假设所有指令固定为 4 字节；压缩指令为 2 字节，普通指令为 4 字节。
- 其他异常不实现恢复逻辑，调用 `print_trapframe(tf)` 作为兜底。
- 输出格式需兼容 `tools/grade.sh` 对 `Illegal instruction at` 和 `Breakpoint at` 的检查。

**共同要求**

- 保持中断/异常分类、处理行为、`tf->epc` 更新与 `sret` 返回行为一致。
- 除指定两个处理函数及必要的 `static` 辅助实现外，不修改 `trap.c` 中其他函数。

