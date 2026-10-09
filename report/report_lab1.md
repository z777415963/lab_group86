# 操作系统实验报告

## 实验基本信息

| 项目 | 内容 |
|------|------|
| **实验名称** | Lab 1: 最小可执行内核 |
| **小组成员** | 2412412-郑志昊、2413498-陈梓轩、2412528-赵甫文 |
| **完成日期** | 2026年10月8日 |

### 小组分工

练习如何分工？

| 成员 | 负责的练习/模块 |
|------|----------------|
| 2412412-郑志昊 | [练习1] |
| 2413498-陈梓轩 | [练习2] |
| 2412528-赵甫文 | [跑通全流程] |

---

## 一、实验目的

1. 理解内核的引导与加载过程，弄清为什么内核必须被加载到物理地址 `0x80200000`。
2. 掌握链接脚本控制内核内存布局与入口点的方法。
3. 理解汇编入口 `kern_entry` 的作用：建立内核栈、把控制权交给 C。
4. 理解 RISC-V 的 M/S/U 特权级，掌握用 `ecall` 跨特权级调用 OpenSBI 服务的方法。
5. 在无标准库的环境下，理解 `cprintf` 是如何自底向上构建出来的。
6. 掌握编译、链接、`objcopy` 生成 bin 镜像、QEMU 加载这一套流程。

**预期成果**：内核被加载到 `0x80200000` 并运行，输出 `(THU.CST) os is loading ...` 后进入死循环。

---

## 二、实验环境

你们使用的 AI 工具

| 成员 | AI 编程工具 | 底层模型 | 备注 |
|------|------------|---------|------|
| 2412412-郑志昊 | [Cline] | [DeepSeek-V4-Flash] | [无] |
| 2413498-陈梓轩 | [Codex] | [ChatGPT-5.6-sol] | [无] |
| 2412528-赵甫文 | [Pi] | [ChatGPT-6-Luna] | [无] |

---

## 三、实验整体逻辑分析

### 3.1 本章节的逻辑主线

**核心主题**：在一个"上电之后一无所有"的 RISC-V 机器上，如何让操作系统内核运行起来。

之所以说"一无所有"，是因为开机瞬间 CPU 没有可用的栈指针，内存里没有我们的代码，屏幕上没有任何输出，而操作系统不可能自己把自己加载到内存。因此必须有一个程序先运行：它初始化硬件、把内核搬到内存，再把 CPU 交出去。这个程序就是 bootloader。在 QEMU 模拟的 RISC-V 系统中，这个角色由 OpenSBI 固件扮演。

本章的逻辑主线围绕"两次交接"展开。

第一次交接是由固件到内核。CPU 上电时 pc 被置为 QEMU 模拟的这款 riscv 处理器的复位地址 0x1000，而不是 0x80000000。0x1000 处是 QEMU 预先写好的复位代码，它负责初始化计算机各组件并启动 bootloader。之后，OpenSBI 会将内核镜像 os.bin 加载到 Qemu 物理内存以地址 0x80200000 开头的区域上，并将 CPU 的控制权交给操作系统。想要加载 bin 文件，我们必须先得到内存布局合适的 elf 文件，然后把它转化成 bin 文件，这一过程由链接器与链接脚本负责。

第二次交接是由汇编到 C。0x80200000 处的第一条指令属于 kern_entry。此时内核虽拿到 CPU，但还不能运行 C 代码，因为函数调用需要栈，而 sp 里还是固件遗留的值。于是 kern_entry 先用 la sp, bootstacktop 造出内核栈，再用 tail kern_init 跳转到 kern_init，把控制流交给 C。

进入 `kern_init` 后，内核需要输出信息。但这里出现了一个逻辑问题：在 Linux 下要格式化输出只需 `#include <stdio.h>`，可 libc 的 `printf` 最终依赖操作系统内核提供的系统调用——**我们正在开发的这个操作系统还不存在，不可能反过来依赖另一个操作系统提供的运行。** 解决方法是借用 OpenSBI 在 M 模式上的标准函数库，使用 `ecall` 指令实现跨特权级别的调用，由 OpenSBI 提供最原始的"输出一个字符"的能力，然后层层包装，最终得到 `cprintf`。这条链是：

```
kern_init (init.c)
   │  需要格式化输出
   ▼
cprintf / vcprintf (kern/libs/stdio.c)
   │
   ▼
vprintfmt (libs/printfmt.c)
   │  回调 cputch
   ▼
cons_putc (kern/driver/console.c)
   │
   ▼
sbi_console_putchar (libs/sbi.c)
   │
   ▼
sbi_call: mv x17/x10/x11/x12; ecall
   │
   ▼
OpenSBI 在 M 态完成一个字符的输出
```

基于上述逻辑，本章实验按照"依赖关系"的顺序逐步实现各个功能：

**第 1 步：先确定内核的内存布局与入口点（`tools/kernel.ld`）**

这是所有后续工作的前提，也是与上一阶段（OpenSBI）对接的唯一接口。具体做法是：在链接脚本中用 `BASE_ADDRESS = 0x80200000` 和 `. = BASE_ADDRESS` 把位置计数器钉在 `0x80200000`，并用 `ENTRY(kern_entry)` 指出入口符号。**`ENTRY()` 只是把入口写进 ELF 头的 `e_entry` 字段，它本身并不会让程序自动从那里开始执行；真正保证这一点的机制是 `.text` 段的第一个输入段是 `*(.text.kern_entry ...)`——即把 `kern_entry` 所在的代码物理地安排在 `0x80200000`。** 二者必须成对使用。

**第 2 步：再实现汇编入口，建立内核栈（`kern/init/entry.S`）**

C 代码的运行需要栈。有了明确的内存布局之后，才可能在 `.data` 段里预留出一块确定地址的栈空间；而"设置 `sp`"这件事是 C 语言无法表达的，只能由汇编完成。这是**汇编层必需为 C 层做的事情**。

在这一步中，用 `.space KSTACKSIZE` 预留 8 KiB 的内核栈（`KSTACKPAGE = 2`、`PGSIZE = 4096`），用 `.align PGSHIFT`（12 位，即 4 KiB）保证页对齐，并用 `la sp, bootstacktop` 把栈顶地址装入 `sp`。同时用 `tail kern_init` 把控制权交给 C。

**第 3 步：然后实现 C 入口，完成内核初启（`kern/init/init.c`）**

栈备好之后，就可以运行 C 代码了。`kern_init` 承担两项"内核初启"的核心任务：

- **初始化内核环境**：执行 `memset(edata, 0, end - edata)` 清除 `.bss` 段，为未初始化的全局变量赋零值。这件事必须由内核自己做，因为裸二进制镜像（bin）里 `.bss` 不会真的占那么多字节，加载者也不会替你清零（详见四、功能模块 3）。
- **向用户提供可视化反馈**：通过 `cprintf("%s\n\n", message)` 输出 `(THU.CST) os is loading ...`，这是操作系统与开发者/用户的第一次交互。

这一步一执行，就会立刻暴露出对新功能的需求：`cprintf` 如何实现？

**第 4 步：向下打通字符输出通道（`kern/driver/console.c` + `libs/sbi.c`）**

SBI 服务是整条封装链的起点，也是最靠近硬件的部分。首先要做的是**跨特权级调用固件服务**：内核运行在 S 态，SBI 服务运行在 M 态，普通函数调用（`call`）无法跨越特权边界，必须使用 `ecall` 指令。同时 `ecall` 需要按固定约定传递参数（功能号放 `x17`，参数放 `x10–x12`，返回值从 `x10` 取），而 C 语言无法精确控制变量落在哪个物理寄存器上，**因此必须借助内联汇编**。

**第 5 步：用构建系统把这一系列步骤串起来（`Makefile` + `tools/function.mk`）**

最终我们能实现"编译所有源码 → 链接出 elf → `objcopy` 成 bin 镜像 → 用 QEMU 加载运行"一整条通路。Makefile 依赖 `tools/function.mk` 中定义的 `add_files_cc`、`create_target` 等函数，自动遍历 `KSRCDIR`/`LIBDIR` 目录收集源文件并推导依赖关系。写得足够好的 Makefile 让整个流程收敛为一条 `make qemu`。

---

## 四、功能理解

### 功能模块 1：内核内存布局与入口点设置

#### 模块功能描述

`tools/kernel.ld`：

```ld
OUTPUT_ARCH(riscv)
ENTRY(kern_entry)               /* 指定入口点符号 */

BASE_ADDRESS = 0x80200000;

SECTIONS
{
    . = BASE_ADDRESS;           /* "." 是位置计数器，赋值为内核起始地址 */

    .text : {
        *(.text.kern_entry .text .stub .text.* .gnu.linkonce.t.*)
    }
    PROVIDE(etext = .);         /* 代码段结束 */

    .rodata : { *(.rodata .rodata.* .gnu.linkonce.r.*) }

    . = ALIGN(0x1000);          /* 数据段对齐到下一个内存页 */

    .data  : { *(.data) *(.data.*) }
    .sdata : { *(.sdata) *(.sdata.*) }

    PROVIDE(edata = .);         /* .bss 开始 */

    .bss : { *(.bss) *(.bss.*) *(.sbss*) }

    PROVIDE(end = .);           /* .bss 结束 */

    /DISCARD/ : { *(.eh_frame .note.GNU-stack) }
}
```

**理解：**

这个脚本解决的是内核"放在哪、从哪开始跑"的问题。其中的 `.` 是位置计数器，`. = BASE_ADDRESS` 把内核的第一个字节钉在 `0x80200000`，之后的各段就依次从这里排下去。需要特别留意的是 `ENTRY(kern_entry)` 的作用容易被误解：它只把入口写进 ELF 头的 `e_entry` 字段，并不会让程序自动从那里开始执行；真正让 `kern_entry` 落在 `0x80200000` 的是段排列顺序——`.text` 的第一个输入段是 `*(.text.kern_entry)`。二者必须成对使用，缺一不可。另外，`PROVIDE(etext/edata/end)` 定义的是标记边界的地址符号，本身不占内存，只供 C 代码取地址；`/DISCARD/` 用来丢弃异常处理等无用段。

再看段的划分，`.bss` 专门存放"要被初始化为 0"的数据，因此可执行文件里只需记录它的位置和大小，不必真的存下那些零——这正是它必须在运行期清零的原因。段序中用 `ALIGN(0x1000)` 把只读段与可写数据段分到不同页上，也为后续实验的页级权限保护留了余地。

---

### 功能模块 2：汇编入口与内核栈的建立

#### 模块功能描述

`kern/init/entry.S`：

```asm
#include <mmu.h>
#include <memlayout.h>

    .section .text,"ax",%progbits
    .globl kern_entry
kern_entry:
    la sp, bootstacktop

    tail kern_init

.section .data
    # .align 2^12
    .align PGSHIFT
    .global bootstack
bootstack:
    .space KSTACKSIZE
    .global bootstacktop
bootstacktop:
```

相关常量（`kern/mm/mmu.h`、`kern/mm/memlayout.h`）：

```c
#define PGSIZE          4096
#define PGSHIFT         12
#define KSTACKPAGE          2
#define KSTACKSIZE          (KSTACKPAGE * PGSIZE)   /* = 8192 字节 */
```

**理解：**

`entry.S` 只做一件汇编层无法回避的事——建立内核栈。C 代码一调用函数就要用栈保存返回地址、保护寄存器和局部变量，而"设置 `sp`"是 C 语言表达不出来的，只能在这里完成。代码开头包含 `<mmu.h>` 与 `<memlayout.h>`，让汇编与 C 共用 `PGSHIFT`、`KSTACKSIZE` 这两个宏，避免把 4096、8192 写死在两个地方。`.section .text,"ax",%progbits` 标明该段可分配、可执行且含实际数据，`.globl` 则让链接器看得到 `kern_entry`，`ENTRY(kern_entry)` 才引用得到。

栈区选择放在 `.data` 而不是 `.bss`，是因为 `kern_init` 只清 `.bss`（`memset(edata, 0, end - edata)`），放在 `.data` 就不会被误清零，`.bss` 的大小语义也不会被这 8 KiB 栈污染；同时要注意 `.space` 在 `.data` 中是会真实占据镜像空间的。`bootstacktop` 是栈区的高地址端（即 `bootstack + 8192`），因为 RISC-V 的栈满递减，压栈时 `sp` 先减后写，这样访问范围才始终落在 `[bootstack, bootstacktop)` 内。

---

### 功能模块 3：C 入口与 `.bss` 段清零


#### 模块功能描述

`kern/init/init.c`：

```c
#include <stdio.h>
#include <string.h>
#include <sbi.h>
int kern_init(void) __attribute__((noreturn));

int kern_init(void) {
    extern char edata[], end[];
    memset(edata, 0, end - edata);

    const char *message = "(THU.CST) os is loading ...\n";
    cprintf("%s\n\n", message);
   while (1)
        ;
}
```

**理解：**

`kern_init` 完成两项"内核初启"的工作：清 `.bss`，以及给出第一句可视化反馈。这里最容易看错的是那两个 `#include`——`<stdio.h>`、`<string.h>` 并不是 C 标准库，而是我们自己写的头文件，Makefile 中的 `-nostdinc -fno-builtin -nostdlib` 从编译层面禁止使用系统库。另外，`__attribute__((noreturn))` 与 `entry.S` 中的 `tail` 构成一对契约：C 侧承诺不返回，汇编侧才敢不保存返回地址，函数末尾的 `while (1) ;` 就是这一承诺的兑现。`edata`、`end` 则是链接器用 `PROVIDE()` 生成的边界符号，只能取地址参与指针运算，`end - edata` 恰好是 `.bss` 段的字节数。

为什么这条 `memset` 必须由内核自己做？因为 `.bss` 在可执行文件中只记录"位置 + 大小"，并不存储实际的零数据，而 C 语言"未初始化全局变量初值为 0"的承诺总得有人在运行期真正执行一次：有操作系统时由内核的加载器负责，没有操作系统时（本实验）只能由被加载者自己负责。这也顺带解释了栈为什么要放在 `.data`（不被这条 `memset` 波及），以及为什么必须有这两个边界符号。

---

### 功能模块 4：通过 OpenSBI 封装字符输出

#### 模块功能描述

`libs/sbi.c`：

```c
#include <sbi.h>
#include <defs.h>

uint64_t SBI_SET_TIMER = 0;
uint64_t SBI_CONSOLE_PUTCHAR = 1;
uint64_t SBI_CONSOLE_GETCHAR = 2;
/* ... 其余功能号 ... */

uint64_t sbi_call(uint64_t sbi_type, uint64_t arg0, uint64_t arg1, uint64_t arg2) {
    uint64_t ret_val;
    __asm__ volatile (
        "mv x17, %[sbi_type]\n"     // 功能编号 -> x17 (a7)
        "mv x10, %[arg0]\n"         // arg0 -> x10 (a0)
        "mv x11, %[arg1]\n"         // arg1 -> x11 (a1)
        "mv x12, %[arg2]\n"         // arg2 -> x12 (a2)
        "ecall\n"                   // 陷入 M 态
        "mv %[ret_val], x10"        // 返回值在 a0
        : [ret_val] "=r" (ret_val)
        : [sbi_type] "r" (sbi_type), [arg0] "r" (arg0),
          [arg1] "r" (arg1), [arg2] "r" (arg2)
        : "memory"
    );
    return ret_val;
}

void sbi_console_putchar(unsigned char ch) {
    sbi_call(SBI_CONSOLE_PUTCHAR, ch, 0, 0);
}
```

`kern/driver/console.c`：

```c
#include <sbi.h>
#include <console.h>

void kbd_intr(void) {}      
void serial_intr(void) {}
void cons_init(void) {}

void cons_putc(int c) { sbi_console_putchar((unsigned char)c); }

int cons_getc(void) {
    int c = 0;
    c = sbi_console_getchar();
    return c;
}
```

**理解：**

这一模块是整条输出链路里最靠近硬件的一层。OpenSBI 可以理解为预先安装在 M 态的一套标准函数库，而内核运行在 S 态，要请它服务就只能用 `ecall`，不能用普通的 `call`。这里有个值得留意的细节：`ecall` 的语义会随发起者的特权级而改变——U 态执行时陷入 S 态（即系统调用），S 态执行时陷入 M 态（本实验的 SBI 调用），二者机制同源，只是"谁向谁请求服务"不同。调用时功能号放 `a7`（`x17`），参数放 `a0`–`a2`（`x10`–`x12`），返回值在 `a0`，等于在 RISC-V 的 ABI 之上又叠加了一层 SBI 自己的约定。

由于 C 语言既无法执行 `ecall`，也无法指定变量落在哪个物理寄存器上，这段调用只能写成内联汇编；`volatile` 防止它被优化掉，`"memory"` 告知编译器该块可能读写内存。`console.c` 的代码量虽小，价值却在于分层：上层只认识 `cons_putc`，完全不知道 SBI 的存在，将来要换成真实的 UART 驱动，只需改这一层。

### 练习 1：理解内核启动中的程序入口操作

**题目**：阅读 `kern/init/entry.S`，说明 `la sp, bootstacktop` 完成了什么操作、目的是什么？`tail kern_init` 完成了什么操作、目的是什么？

#### （一）`la sp, bootstacktop`

**完成的操作：把内核启动栈的栈顶地址装入 `sp`。**

`la` 是伪指令，不是真实机器指令。它将bootstacktop装入sp.

`bootstacktop` 的地址由汇编与链接脚本共同决定：`.align PGSHIFT`（4 KiB）对齐后的 `bootstack`，加上 `.space KSTACKSIZE` 预留的 8192 字节，紧接的就是 `bootstacktop`，故 `bootstacktop = bootstack + 8192`。`.global` 让这些符号对链接器可见，`la` 的立即数才能被正确回填。

**目的**

1. **必要性**：函数调用要用栈保存返回地址、保护寄存器和局部变量，这些都按 `sp` 寻址；而 RISC-V 硬件不会自动设置 `sp`，进入 `kern_entry` 时它还是 OpenSBI 遗留的值。若直接跳到 C 代码，第一次调用（`kern_init` 调 `memset`）就会往一个不受控的地址压栈，破坏固件或内核数据，甚至触发非法访存。这是**汇编层为 C 层做的最基本、不可省略的一件事**。
2. **安全性**：栈区是 `.data` 段中静态预留的 8 KiB，就落在内核镜像内，不会与固件镜像（`0x80000000` 起）重叠，也不会落在来路不明的内存上。
3. **正确性**：RISC-V 栈是满递减的，压栈时先减 `sp` 再写入，因此初始 `sp` 应指向栈区**高地址端** `bootstacktop`。此后入栈都发生在 `[bootstack, bootstacktop)` 内，天然不会越界。

#### （二）`tail kern_init`

**完成的操作：把控制权无条件移交给 C 函数 `kern_init`（`pc ← &kern_init`），且不保存返回地址。**

`tail` 是尾调用伪指令，展开为无返回地址保存的跳转：目标在 `jal` 范围内时即 `jal x0, kern_init`（助记符写作 `j kern_init`），超出范围则展开为 `auipc` + `jalr x0, offset(reg)`。**关键在于目标寄存器是 `x0`**：`jal` 本应把下一条指令的地址写入 `rd`，而 `x0` 是硬连线的零寄存器、写入被丢弃，因此 `ra` 不会被修改。对比之下，`call kern_init` 会展开为 `jal ra, kern_init`，把 `0x80200008` 附近的地址写入 `ra`——**这就是二者最本质的区别**。

**目的：**

1. **完成"汇编入口 → C 入口"的交接**。栈建好后，汇编层已无事可做，清 `.bss`、输出信息以及后续的中断、时钟、内存管理初始化都应当用 C 来写。因此 `kern_entry` 只有两条指令，第二条就是交棒。
2. **不保存返回地址是正确的，因为根本无处可返**。`kern_init` 声明为 `noreturn`，函数体是 `while (1) ;`，永不返回；而 `kern_entry` 的"调用者"是 OpenSBI 通过 `mret` 从 M 态切到 S 态的启动流程，并不存在一个调用栈帧，原先 `ra` 的值已属于固件上下文。既没有要返回的地方，也没有可正常返回的上下文。
3. **用 `call` 反而埋下隐患**。若写了 `ra = 0x80200008`，一旦将来某处误执行 `ret`，就会跳到 `kern_entry` 中跳转指令之后的位置，那里的内容未必有意义，可能导致难以定位的崩溃。`tail` 从源头消除了这种可能。
4. **更紧凑，语义也更诚实**。它少了一次被丢弃的寄存器写入，更重要的是助记符本身表达了"这里是尾调用、不是调用一个会回来的函数"。对内核代码而言，用准确助记符表达意图比省一条指令更有价值。

#### （三）两条指令的关系

- `la sp, bootstacktop` 准备空间（栈）；
- `tail kern_init` 移交控制流。

二者恰好对应内核启动的两次交接中的第二次：`OpenSBI → 内核`是特权级与代码的交接。

---

### 练习 2：使用 GDB 验证启动流程

**题目**：使用 GDB 跟踪 QEMU 模拟的 RISC-V 从加电开始，直到执行内核第一条指令（跳转到 `0x80200000`）的整个过程。RISC-V 硬件加电后最初执行的几条指令位于什么地址？它们主要完成了哪些功能？

#### 调试步骤

**1. 跟踪复位向量**

```gdb
(gdb) b *0x1000
(gdb) si
(gdb) x/6i $pc
(gdb) info registers x10 x11 x12 pc
```

复位向量（QEMU virt 机器 MROM 基址 `0x1000`）的典型布局与功能：

| 地址 | 指令 | 功能 |
|------|------|------|
| `0x1000` | `auipc t0, %pcrel_hi(fw_dynamic_info)` | 取得 `fw_dynamic_info` 结构体地址（紧跟复位向量） |
| `0x1004` | `addi a2, t0, %pcrel_lo(...)` | `a2 ← &fw_dynamic_info`（含 `next_addr`、`next_mode`） |
| `0x1008` | `csrr a0, mhartid` | `a0 ← 当前 hart id`（本实验为 0） |
| `0x100c` | `ld a1, 32(t0)` | `a1 ← 设备树（FDT/DTB）地址` |
| `0x1010` | `ld t0, 24(t0)` | `t0 ← fw_dynamic_info.next_addr`（即 `0x80000000`） |
| `0x1014` | `jr t0` | 跳转到 `0x80000000`，进入 OpenSBI |

不同 QEMU 版本的复位向量实现略有差异（例如是否在此处设置 `a2`），**以 `x/6i 0x1000` 的实际反汇编为准**，但"设置 hart id / 设备树指针 / 跳转固件入口"这三大功能不变。

**2. 用 watchpoint 跳过 OpenSBI，捕捉内核加载瞬间**

OpenSBI 初始化指令量很大，不要单步跟。用数据断点"谁写内核区谁被抓住"：

```gdb
(gdb) del
(gdb) watch *(unsigned int *)0x80200000
(gdb) continue
(gdb) bt                                  # 看是谁在写内核镜像
```

这说明：内核镜像在**执行任何内核指令之前**就已整块搬到 `0x80200000`。这个位置不是内核自己争取的，而是引导链规定的——OpenSBI 从 `fw_dynamic_info.next_addr` 读到 `0x80200000`，才知道把内核放在哪、将来把 `pc` 跳到哪。

**3. 捕捉控制权移交**

```gdb
(gdb) del
(gdb) b *0x80200000
(gdb) continue
(gdb) info registers pc                   # → pc = 0x80200000
(gdb) x/4i $pc
# 0x80200000: auipc sp, ...     ← la sp, bootstacktop 展开的第一条
# 0x80200004: addi  sp, sp, ... ← la 展开的第二条
# 0x80200008: j     ...         ← tail kern_init 展开（jal x0, ...）
```

**4. 放开运行**

```gdb
(gdb) b kern_init
(gdb) continue
(gdb) b memset
(gdb) continue
(gdb) finish                              # 跑完清 .bss
(gdb) x/8xb edata
(gdb) continue
```

终端输出：

```
(THU.CST) os is loading ...
```

随后停在 `while (1) ;`（按 `Ctrl-C` 中断可看到 `pc` 停在自跳转的紧凑指令上）。

#### 两个问题的回答

**问题一：RISC-V 硬件加电后最初执行的几条指令位于什么地址？**

**位于 `0x1000`**，即 QEMU virt 机器的 MROM（复位向量）基址。CPU 上电时 `pc` 被硬件置为复位向量地址。RISC-V 规范允许实现者自选复位地址（x86 的 80386 是 `0xFFF0`，MIPS 是 `0x00000000`），`0x1000` 是 QEMU 的选择——本工作区 `libs/riscv.h` 中的 `#define DEFAULT_RSTVEC 0x00001000` 正与此印证。

因此取指起点链是：**`0x1000`（复位向量）→ `0x80000000`（OpenSBI，M 态）→ `0x80200000`（内核，S 态）**，三个地址分别回答"CPU 从哪开始"、"固件在哪"、"内核在哪"。

**问题二：它们主要完成了哪些功能？**

复位向量本身不做硬件初始化，它只做"**打包参数 + 跳转**"，把控制权连同固件运行所需的信息交给 OpenSBI：

1. `a0 ← hart id`（`csrr a0, mhartid`），让固件知道自己跑在哪个核上（本实验为 hart 0，日志中 `Current Hart : 0` 即由此而来）；
2. `a1 ← 设备树地址`，让固件与后续内核能发现内存布局和设备；
3. `a2 ← fw_dynamic_info 地址`。该结构体是 QEMU 与 OpenSBI 之间的接口，`next_addr` 记录内核入口 `0x80200000`，`next_mode` 指明下一阶段特权级；
4. 从 `fw_dynamic_info` 取出固件入口并 `jr` 到 `0x80000000`。这样复位向量无需"知道"固件在哪，位置由数据结构提供。

**完整启动链条：**

```
① 上电：PC ← 0x1000，执行 MROM 复位向量（M 态）
        → 设置 a0=mhartid、a1=dtb、a2=&fw_dynamic_info → 跳到 0x80000000
② OpenSBI（M 态，Base 0x80000000，Size 120 KB）
        → 打印 banner 与平台信息；配置 MIDELEG/MEDELEG；
          设置 PMP0=0x80000000-0x8001ffff、PMP1=全空间
        → 把内核镜像加载到 0x80200000
③ mret 进入 S 态，跳到 next_addr = 0x80200000（控制权移交内核）
④ kern_entry → la sp, bootstacktop → tail kern_init
        → 清 .bss → cprintf("(THU.CST) os is loading ...") → while(1)
```

---

## 五、测试与验证

### 5.1 编译与运行（`make` / `make qemu`）

```bash
make clean && make && make qemu
```

编译阶段应输出各源文件的 `+ cc ...`、`+ ld bin/kernel`，以及 `objcopy` 生成 `bin/ucore.img`。由于 Makefile 使用 `-Werror`，**任何警告都会导致编译失败**。

运行阶段先输出 OpenSBI 的 banner 与平台信息（`Platform Name : QEMU Virt Machine`、`Firmware Base : 0x80000000`、`Firmware Size : 120 KB`、`MIDELEG`/`MEDELEG`、`PMP0`/`PMP1` 等），随后是内核的输出：

```
(THU.CST) os is loading ...

```

之后终端不再有新输出，QEMU 不退出、不报错、不重启。

**判定标准**：出现 `(THU.CST) os is loading ...`；该行与固件输出之间有空行（来自 `cprintf("%s\n\n", message)` 的两个 `\n`）；此后无其他输出且不崩溃。三点同时满足即说明：内核被正确加载到 `0x80200000` 并从 `kern_entry` 执行、内核栈建立正确、`.bss` 清零正常、输出链路 `cprintf → vprintfmt → cputch → cons_putc → sbi_console_putchar → ecall → OpenSBI` 全线打通。

---

## 六、实验总结与收获

### 对操作系统的理解

#### （1）重要知识点及其与 OS 原理的对应

**程序的装入与地址空间。** 本实验最直接的约束是链接地址必须等于加载地址：内核是地址相关代码，只有落在 `0x80200000` 才能正确运行，所以链接脚本的 `BASE_ADDRESS`、QEMU 的 `-device loader,addr=` 与 OpenSBI 的 `next_addr` 三者必须一致。这正对应 OS 原理中"程序装入"这一环节，区别在于真实 OS 的内核往往链接在虚拟地址、加载到物理地址，靠页表把两者接起来，而本实验还没有 MMU，虚拟地址恒等于物理地址，这个等式才得以成立。

**栈与函数调用约定。** `entry.S` 用两条指令换来 C 代码的运行环境，其中 `la sp, bootstacktop` 建的是一块静态预留的 8 KiB 空间，并因 RISC-V 栈满递减而取高地址端做栈顶。这与 OS 原理中的栈、栈帧和调用约定是同一件事，只是真实 OS 中每个进程或线程都有独立的内核栈，由进程创建和上下文切换动态设置，并依赖 MMU 加保护页。

**特权级与跨特权级服务。** `ecall` 是本次实验唯一使用的特权指令：内核在 S 态执行它陷入 M 态，请求 OpenSBI 输出一个字符。同样一条指令在 U 态执行则陷入 S 态，也就是系统调用。二者机制同源（受控陷入加固定寄存器约定），只是方向不同，这说明特权级可以层层叠加，每一层都向下一层请求服务。

本实验真正要打牢的只有两块基石：内存布局（位置）与输出通道（可观测性）。有了可观测性，后续加 trap、时钟、内存管理、进程时都能立刻看到结果。

#### （2）OS 原理中重要、但本实验未覆盖的知识点

1. **中断与异常处理**：没有设置 `stvec`/`mtvec`，没有 trap 处理程序、上下文保存恢复、`scause`/`sepc` 解析与 `sret`。
2. **时钟中断与抢占**：`sbi_set_timer` 已实现却未被调用，因此没有"时间片"概念。
3. **进程与线程、调度算法**：没有 PCB/TCB、`fork/exec/wait/exit`、就绪队列与调度策略，内核只有一个执行流。
4. **物理内存管理**：没有空闲页链表与 `alloc_pages`/`kmalloc`；本实验的 `bootstack` 是编译期预留，不是运行期分配。
5. **虚拟内存与页表**：没有 `satp`、多级页表、TLB 与 `sfence.vma`；`riscv.h` 中的 `PTE_*`、`lcr3` 已备好但无人调用。

---
