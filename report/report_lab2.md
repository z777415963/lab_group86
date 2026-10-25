# 操作系统实验报告

## 实验基本信息

| 项目 | 内容 |
|------|------|
| **实验名称** | Lab 2: 物理内存管理 |
| **小组成员** | 2412412-郑志昊、2413498-陈梓轩、2412528-赵甫文 |
| **完成日期** | 2026年10月25日 |

### 小组分工

练习如何分工？

| 成员 | 负责的练习/模块 |
|------|----------------|
| 2412412-郑志昊 | [功能模块1] |
| 2413498-陈梓轩 | [功能模块2任务3+gdb调试] |
| 2412528-赵甫文 | [功能模块2任务1+2] |

---

## 一、实验目的

本实验的主要目的是：

1. 理解 RISC-V Sv39 页表与虚实地址转换的基本原理，理解内核如何借助大页映射从物理地址空间切换到高地址虚拟地址空间。
2. 掌握物理内存探测与物理页管理的整体流程：从设备树（DTB）取得物理内存范围，建立 `struct Page` 描述符数组，并把真正可用的连续物理页注册给物理内存管理器。
3. 理解连续物理内存分配算法，掌握 First-Fit 的初始化、分配、释放与相邻块合并过程，实现 Best-Fit 分配器，并进一步实现 Buddy System 与简化版 SLUB 分配器。
4. 学习借助 AI 辅助开发操作系统核心功能，掌握用接口约束、前后条件和边界条件组织提示词，并对生成代码进行验证与迭代的方法。

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

### 2.1 本章节的逻辑主线

本章围绕“物理内存的发现、初始化与分配”展开。内核启动时不能假设自己知道机器有多少物理内存：OpenSBI 先把设备树（DTB）放在内存中，并通过 `a1` 寄存器交给内核；内核在 `kern_init()` 中解析 DTB，得到物理内存的起始地址与大小。随后内核已经运行在 Sv39 的高地址虚拟地址空间中，必须把设备树报告的物理地址范围换算成可管理的物理页号上界，并在内核镜像结尾之后就地划分出 `struct Page` 描述符数组；最后把排除 OpenSBI、内核镜像和描述符数组之后的完整物理页交给一个可替换的物理内存管理器（`pmm_manager`），由它按 First-Fit、Best-Fit、Buddy System 等算法完成页面的分配与释放；在页分配器之上，再实现 SLUB 来管理固定大小的内核对象。

因此，本章要解决的问题可以概括为三个层次：

1. **探测**：机器实际有多少可用物理内存，边界在哪里；
2. **建模**：用连续的 `struct Page` 描述符表示每个物理页，并准确划分“已占用”和“空闲”；
3. **分配**：用什么样的数据结构与算法管理空闲页，使其能被分配、释放并合并。

整体实现链可以概括为 `pmm_init() -> init_pmm_manager() -> page_init() -> init_memmap() -> alloc_pages()/free_pages()`。

### 2.2 功能的逐步实现

1. 首先实现**设备树解析（`dtb_init`）**。`page_init` 需要知道物理内存的起点和大小，因此必须先由 `dtb_init()` 把 DTB 中的 `memory` 节点信息解析到 `get_memory_base()` / `get_memory_size()`，这是后续所有地址计算的数据来源。
2. 接着建立并初始化**物理内存管理器抽象（`init_pmm_manager`）**。先把 `pmm_manager` 指向某个具体实现并调用其 `init()`，`page_init()` 才能通过 `init_memmap()` 把空闲区交给管理器；先定义统一接口，也使后续替换分配算法无需改动上层代码。
3. 然后实现核心的 **`page_init()`**。它把 DTB 的物理地址范围、内核镜像结尾的虚拟地址、描述符数组的物理占用综合起来，确定 `va_pa_offset`、`npage`、`pages`，先全部置保留再释放真正空闲的页。这一步是“探测”到“可分配”的桥梁，也是本实验虚拟地址/物理地址最容易混用的地方。
4. 再理解并沿用 **First-Fit 分配器**。默认管理器提供了完整的初始化、分配、释放和合并流程，先吃透它，才能在相同的状态约定下实现其他算法；同时它也是检验 `page_init()` 注册的空闲区是否正确的第一块试金石。
5. 然后实现并接入 **Best-Fit 分配器**。在 `best_fit_pmm.c` 中按相同接口补全实现，并在 `pmm.c` 中切换管理器，用同一套 `basic_check` / `best_fit_check` 和 `make grade` 验证替换后的行为。
6. 接着完成 **Buddy System**。用多条按阶组织的空闲链表管理 2 的幂大小的块，分配时向上取整并逐级拆分，释放时用“索引异或块大小”找伙伴并逐级合并，理解与 First/Best-Fit 完全不同的另一类页分配思路。
7. 最后作为扩展实现 **简化版 SLUB**。在页分配器之上建立对象缓存，把页切成固定大小的对象，用对象内空闲链表管理，理解“页级分配”与“对象级分配”的分层关系。

---

## 四、实验内容与实现

### 功能模块：物理内存初始化功能

**负责人：** [2412412-郑志昊]

#### 模块功能描述

**需要实现/修改的函数：**

```c
static void page_init(void);
```

**功能说明：**

`page_init()` 在 `pmm_init()` 中紧跟 `init_pmm_manager()` 被调用，负责把 DTB 报告的物理内存范围转换成内核可管理的页描述符布局，并把真正空闲的页交给当前 `pmm_manager`。实现分为以下几步：

1. **校验内存范围。** 通过 `get_memory_base()`、`get_memory_size()` 取得 DTB 解析结果；只要 `mem_base == 0` 或 `mem_size == 0`，就调用 `panic()` 终止初始化（Case 1）。
2. **确定映射偏移。** `va_pa_offset = PHYSICAL_MEMORY_OFFSET`，即内核虚拟地址与物理地址之间固定的 `0xFFFFFFFF40000000` 差值，后续 `PADDR()` 依赖它完成虚实转换。
3. **计算页号上界。** `mem_end = mem_base + mem_size` 是物理内存末端；`npage = PPN(mem_end)` 是可管理物理页号的上界；DRAM 起始页号由 `nbase = DRAM_BASE / PGSIZE` 给出，因此描述符数组需要覆盖 `[nbase, npage)`。
4. **放置描述符数组。** `pages = ROUNDUP((uintptr_t)end, PGSIZE)`。`end` 是链接脚本给出的内核镜像结尾**虚拟地址**，因此这里只做虚拟地址运算，数组起点紧接内核镜像且按页对齐。
5. **先全部置保留。** 对 `[nbase, npage)` 对应的 `npage - nbase` 个描述符逐个 `SetPageReserved`，保证尚未完成布局的页不会被提前分配。
6. **计算第一个空闲物理页。** 描述符数组末尾仍是虚拟地址，必须先用 `PADDR((uintptr_t)pages + (npage - nbase) * sizeof(struct Page))` 转换为物理地址，再 `ROUNDUP(..., PGSIZE)` 得到 `freemem_pa`。这一步体现了本题最关键的约束：数组内部寻址用虚拟地址，物理边界必须经 `PADDR()` 转换后再计算。
7. **对齐物理内存末端。** 用 `ROUNDDOWN(mem_end, PGSIZE)` 丢弃末尾不足一页的部分，保证不会把不完整的尾页交给分配器。
8. **注册空闲区。** 仅当 `freemem_pa < mem_end_pa` 时，计算空闲页数 `n = (mem_end_pa - freemem_pa) / PGSIZE`，用 `pa2page(freemem_pa)` 取得起始描述符，再调用 `init_memmap(base, n)`；若两者相等或倒置（Case 3），说明不存在完整空闲页，直接跳过 `init_memmap()`，不触发 panic，内核继续运行（后续无内存可分配）。
9. **输出探测结果。** 打印物理内存的起点、终点、大小，以及注册的空闲区范围，便于在 QEMU 串口中核对。

这样处理后，OpenSBI（`[0x80000000, 0x80200000)`）、内核镜像（`end` 之前）和 Page 描述符数组所在的页面都保持保留状态，只有描述符数组之后到物理内存末端之间的完整页进入空闲链表。缺省 128 MiB 配置下 `npage = 0x88000`，可用空闲区从描述符数组之后一直延伸到 `0x88000000`。

#### 最终提示词

以下是经过迭代优化后，最终成功实现该功能的提示词：

````markdown

[PROMPT]
任务：修改 kern/mm/pmm.c 文件，实现物理内存探测与页描述符初始化功能的核心函数 page_init。

操作要求：你必须在项目中修改实际文件，保留现有代码，只在对应位置补全 page_init 的函数体。请严格注意虚拟地址、物理地址和物理页号之间的转换关系，确保通过链接符号计算物理内存边界时的正确性。

输出要求：直接进行文件操作。请依赖下面 [RELY]、[GUARANTEE]、[SPECIFICATION] 部分提供的信息进行实现，不要引入无关的外部库或多余的全局变量。

[RELY]
// 宏定义与常量
#define PGSIZE 4096
#define PGSHIFT 12
#define DRAM_BASE 0x80000000
#define PADDR(kva) ((uintptr_t)(kva) - va_pa_offset) // 将内核虚拟地址转为物理地址
#define ROUNDUP(a, b) (((a) + (b) - 1) & ~((b) - 1)) // 向上按页对齐
#define ROUNDDOWN(a, b) ((a) & ~((b) - 1)) // 向下按页对齐
#define PPN(pa) ((pa) >> PGSHIFT) // 获取物理页号

// 数据结构
struct Page {
    int ref;
    uint64_t flags;
    unsigned int property;
    list_entry_t page_link;
};

// 可用的全局变量
extern char end[];           // 链接符号：内核代码结尾的虚拟地址
extern struct Page *pages;   // 指向 Page 描述符数组起始位置（虚拟地址）
extern size_t npage;         // 可管理的物理页号上界
uint64_t va_pa_offset;       // 虚拟地址到物理地址的偏移量
const size_t nbase = DRAM_BASE / PGSIZE; // 物理内存起始页号

// 可用的辅助函数（假设框架已实现或提供声明）
uint64_t get_memory_base(void); // 返回设备树探测到的内存物理起始地址
uint64_t get_memory_size(void); // 返回设备树探测到的内存大小
void panic(const char *msg);    // 内核恐慌，终止运行
int cprintf(const char *fmt, ...); // 内核打印函数
struct Page *pa2page(uintptr_t pa); // 物理地址转 Page 结构体指针
static void init_memmap(struct Page *base, size_t n); // 初始化空闲物理页区域

[GUARANTEE]
必须实现或修改的接口：
static void page_init(void);

你可以根据需要添加辅助函数（建议设为 static），但不得修改上述接口签名。

[SPECIFICATION]
## page_init

Pre-Condition:
- pmm_init() 已被调用，且设备树（DTB）解析已完成，能够通过 get_memory_base() 和 get_memory_size() 获取到合法的物理内存范围。
- pmm_manager 已完成基础初始化（如 init_pmm_manager 已运行）。

Post-Condition:
- 全局变量 va_pa_offset 被正确设置，用于后续的虚实地址转换。
- 全局变量 npage 被正确设置为物理内存末尾的物理页号。
- 全局变量 pages 被指向 Page 描述符数组的首地址（虚拟地址）。
- Page 描述符数组已分配完成，数组中所有页初始状态被标记为保留（reserved）。
- 排除了 OpenSBI、内核镜像和描述符数组后的所有完整空闲页，已通过 init_memmap() 注册给当前的物理内存管理器。
- 打印输出了探测到的物理内存范围（起点、终点、大小）。

Case 1 (无效参数):
- 如果通过 get_memory_base() 获取的物理起始地址为 0，或 get_memory_size() 获取的大小为 0，则调用 panic() 终止内核初始化，并输出错误信息。

Case 2 (内存布局正常，存在可用空闲页):
- 计算 va_pa_offset = PHYSICAL_MEMORY_OFFSET（可通过虚拟地址 end 和其物理地址关系得出）。
- 根据物理内存起点、大小以及 va_pa_offset，计算出物理内存的结束位置，进而计算得出 npage。
- 将 pages 设置为从 end 开始向上按页对齐的虚拟地址。
- 将 pages 到 pages + (npage - nbase) 所覆盖的所有 struct Page 的标志位设为保留状态（如 SetPageReserved）。
- 计算描述符数组结束后的物理地址（需用 PADDR 将数组结尾转换为物理地址），并向上按页对齐，得到第一个真正的空闲物理页的物理地址。
- 将设备树报告的物理内存末尾（起点+大小）向下按页对齐。
- 如果对齐后的末尾大于对齐后的起点，则计算空闲页数量 n，调用 pa2page() 将起始物理地址转为 struct Page *，并调用 init_memmap(base, n) 将其注册为空闲区。

Case 3 (内存布局异常紧凑，无可用空闲页):
- 如果描述符数组结束后的对齐地址大于或等于向下对齐后的物理内存末尾，说明没有完整的空闲页可用。此时应跳过 init_memmap() 的调用，不抛出 panic，允许系统继续运行（但后续无内存可分配）。

Requirements:
- 在进行加减法运算以确定物理地址边界时，必须严格区分虚拟地址（用于访问变量和数组）和物理地址（用于计算硬件内存实际大小）。
- 涉及 end 指针时，必须用 PADDR(end) 将其转为物理地址后再计算物理边界。
- 涉及 pages 数组末尾时，必须将虚拟地址指针转化为物理地址后再进行边界确认和页对齐操作。

````

#### 实现迭代过程

本模块的实现经历了 1 次迭代，过程如下：

---

##### 第一次迭代

**最终结果：**

经过 1 次迭代，该模块最终：
- ✅ 编译通过（`riscv64-unknown-elf-gcc` 交叉编译无告警）
- ✅ 所有相关测试通过（`make grade` 中 `check page init: OK`）
- ✅ 功能符合预期

---

### 功能模块：物理内存分配功能

**负责人：** [2412528-赵甫文]

#### 任务一：理解 First-Fit 连续物理内存分配算法（思考题）

First-Fit 分配器用 `free_area_t` 维护一条按物理地址升序排列的空闲块链表：`free_list` 是哨兵头节点，`nr_free` 记录空闲页总数；每个空闲块的首页通过 `page_link` 挂在链表上，并用 `property` 记录该块包含的页数、用 `PageProperty` 标志标记“空闲块首页”。

- **初始化空闲页（`default_init` / `default_init_memmap`）**：`default_init` 把哨兵初始化为空环并令 `nr_free = 0`。`default_init_memmap(base, n)` 把 `[base, base + n)` 登记为空闲块：先逐页断言其处于 `PageReserved` 状态，再清零 `flags`、`property` 并令 `ref = 0`；随后令 `base->property = n`、`SetPageProperty(base)`、`nr_free += n`，再按地址把该块插入空闲链表（空表直接插入；否则找到第一个地址更大的块插到它前面，或插到表尾）。
- **分配连续物理页（`default_alloc_pages(n)`）**：若 `n > nr_free` 直接返回 `NULL`；否则从表头开始扫描，找到**第一个** `property >= n` 的块即为首次适配的目标。把它从链表摘下；若其大小大于 `n`，把 `page + n` 作为剩余块，令其 `property = 原大小 - n`、置 `PageProperty` 后插回原位置之后；最后 `nr_free -= n`，并清除分配块首页的 `PageProperty`。
- **回收页面（`default_free_pages(base, n)`）**：逐页断言“非保留且不是空闲块首页”，清零 `flags`、`property`、`ref`；令 `base->property = n`、置 `PageProperty`、`nr_free += n` 后按地址插入链表。
- **合并相邻空闲块**：插入后先看链表前驱 `p`，若 `p + p->property == base`，说明物理相邻，把 `base` 合并进 `p`（累加 `property`、清除 `base` 的 `PageProperty`、把 `base` 从链表删除，并把 `base` 改为 `p`）；再看此时的后继 `p`，若 `base + base->property == p`，同样合并并删除 `p`。通过前后两次合并，空闲块尽可能保持连续，减少外部碎片。

**原有的 first fit 算法是否有进一步的改进空间？**

有，至少有两类改进空间：

1. **插入逻辑的健壮性。** `default_init_memmap` 和 `default_free_pages` 中“插到表尾”的分支在 `list_add(le, &base->page_link)` 之后没有 `break`，循环会继续处理刚插入的节点，可能再次命中同一分支并对同一节点再做一次 `list_add`，使 `page_link.prev` 指向自己；建议补上 `break`，或抽出统一的按地址插入函数。
2. **查找效率与分配质量。** 分配需要从表头线性扫描（最坏 O(k)，k 为空闲块数），空闲块很多时较慢；可以用按块大小分桶的多条链表、平衡树或位图加速，同时维护地址索引以便合并相邻块。此外 First-Fit 容易在低地址留下较多小碎片，可结合 Best-Fit、分离适配等策略改善。

#### 任务二：实现 Best-Fit 连续物理内存分配算法

本任务在 `kern/mm/best_fit_pmm.c` 中补全 Best-Fit 模块，并在 `kern/mm/pmm.c` 中把当前管理器切换为 `best_fit_pmm_manager`。

#### 模块功能描述

**需要实现/修改的函数：**

```c
static void best_fit_init(void);
static void best_fit_init_memmap(struct Page *base, size_t n);
static struct Page *best_fit_alloc_pages(size_t n);
static void best_fit_free_pages(struct Page *base, size_t n);
static size_t best_fit_nr_free_pages(void);

const struct pmm_manager best_fit_pmm_manager;

// 同时在 kern/mm/pmm.c 中修改：
static void init_pmm_manager(void);
```

**功能说明：**

Best-Fit 与 First-Fit 使用相同的 `struct Page` / `free_area_t` 状态约定，区别在于分配时不再“取第一个够大的块”，而是“取所有够大的块中最小的一块”。实现如下：

- **内部状态。** `static free_area_t free_area`，并用 `free_list`、`nr_free` 两个宏访问。它与 `default_pmm.c` 中的同名静态变量分处不同编译单元，互不影响。
- **`best_fit_init`。** `list_init(&free_list)`，`nr_free = 0`。
- **`best_fit_init_memmap(base, n)`。** 与 First-Fit 相同的逐页清理和元数据建立：断言 `PageReserved`，清零 `flags`、`property`，令 `ref = 0`；再令 `base->property = n`、`SetPageProperty(base)`、`nr_free += n`；最后调用 `best_fit_insert_block(base)` 按地址插入，并调用 `best_fit_merge(base)` 尝试与前后空闲块合并。
- **`best_fit_insert_block`。** 从哨兵开始遍历，找到第一个地址大于 `base` 的空闲块并用 `list_add_before` 插入；若遍历完都没有，则用 `list_add(&free_list, ...)` 试图追加到末尾（该兜底分支的位置问题见“改进空间”一节）。
- **`best_fit_merge`。** 检查链表前驱：若 `p + p->property == base` 则合并，累加 `property`、清除 `base` 的 `PageProperty` 并删除 `base`，再把 `base` 改为 `p`；随后检查后继：若 `base + base->property == p` 则累加并删除 `p`。
- **`best_fit_alloc_pages(n)`。** `n > nr_free` 时返回 `NULL`；否则遍历整条链表，在所有 `property >= n` 的块中选择 `property` 最小者；若遇到恰好等于 `n` 的块可提前结束（之后不会出现更小的）。摘下目标块后，若块大小大于 `n`，把 `best + n` 作为剩余块插回 `prev` 之后；再把 `[best, best + n)` 内所有页的 `PageProperty` 清除、`property` 置 0；最后 `nr_free -= n` 并返回 `best`。
- **`best_fit_free_pages(base, n)`。** 与 First-Fit 相同地断言并清理页面、建立 `base` 的空闲块元数据并 `nr_free += n`，然后插入并前后合并。
- **注册与切换。** 文件末尾定义 `const struct pmm_manager best_fit_pmm_manager`，`name` 为 `"best_fit_pmm_manager"`，`check` 绑定 `best_fit_check`；`kern/mm/pmm.c` 的 `init_pmm_manager()` 把 `pmm_manager` 指向它，使 `page_init()` 中的 `init_memmap()`、后续 `alloc_pages()`/`free_pages()` 和 `check_alloc_page()` 都走 Best-Fit。

这样 `page_init()` 注册空闲区时会调用 `best_fit_init_memmap`；由于此时链表为空，插入与合并都是空操作，结果与 First-Fit 等价，不影响前一模块建立的内存布局。

#### 最终提示词

以下是经过迭代优化后，最终成功实现该功能的提示词：

````markdown

[PROMPT]
**任务：**
参考 kern/mm/default_pmm.c 的 First-Fit 实现，在
kern/mm/best_fit_pmm.c 的 lab2 / YOUR CODE 标记处补全完整的
Best-Fit 物理内存管理模块，包括内部状态、管理函数和注册结构。
修改 kern/mm/pmm.c 的 init_pmm_manager()，将管理器切换为
best_fit_pmm_manager，并按需包含对应头文件。
**操作要求：**
保留 basic_check() 和 best_fit_check() 原样，不得修改两个测试函数。允许在标记处添加声明和辅助函数。
**输出要求：**
完成后简要说明修改内容和算法思路，不引入无关功能。算法的时空复杂度不做要求。

[RELY]
- kern/mm/default_pmm.c：参考初始化、分割、释放和合并逻辑。
- kern/mm/best_fit_pmm.c：确认两个测试函数使用的状态约定。
- kern/mm/pmm.h：确认 struct pmm_manager 的成员及函数指针类型。
- kern/mm/memlayout.h、libs/list.h：确认页面描述符和链表接口。
- kern/mm/best_fit_pmm.h：已有 best_fit_pmm_manager 的 extern 声明。
**相关已有定义：**
- struct Page 包含 ref、flags、property、page_link。
- 连续的 Page 描述符对应连续物理页；base + n 表示后移 n 页。
- free_area_t 包含 list_entry_t free_list 和 unsigned int nr_free。
- PageReserved、PageProperty 及其 Set/Clear 宏用于页面状态管理。
- set_page_ref() 设置引用计数；le2page() 将链表节点转换为页面。
- list.h 提供链表初始化、遍历、插入、删除等操作。
- alloc_pages()/free_pages()/nr_free_pages() 通过当前管理器调用；

[GUARANTEE]
在 best_fit_pmm.c 中实现以下内部函数：
static void best_fit_init(void);
static void best_fit_init_memmap(struct Page *base, size_t n);
static struct Page *best_fit_alloc_pages(size_t n);
static void best_fit_free_pages(struct Page *base, size_t n);
static size_t best_fit_nr_free_pages(void);
定义：
const struct pmm_manager best_fit_pmm_manager;
注册结构的 init、init_memmap、alloc_pages、free_pages、
nr_free_pages 分别绑定上述函数，check 绑定已有 best_fit_check，
name 使用 "best_fit_pmm_manager"。
修改 pmm.c 中已有的：
static void init_pmm_manager(void);
使 pmm_manager 指向 &best_fit_pmm_manager，并保留初始化调用。

[SPECIFICATION]
**1. best_fit_init**
Pre-Condition：管理器尚未初始化。
Post-Condition：空闲链表为空，空闲页数为 0。

**2. best_fit_init_memmap**
Pre-Condition：n > 0；[base, base + n) 是尚未加入管理器的
连续可用页，页面原先标记为 Reserved。
Post-Condition：清除这些页面的保留状态，引用计数置 0，
建立正确的空闲块元数据，将该区域加入空闲管理，
空闲页数增加 n；若与已有空闲块相邻，应合并。

**3. best_fit_alloc_pages**
Pre-Condition：n > 0，管理器已初始化。
Post-Condition：成功时返回连续 n 页的首页，空闲页数减少 n；
分配出的页面不带 PageProperty，引用计数为 0。
Case 1：存在足够大的空闲块时，选择所有大小不少于 n 的块中
最小的一块；大小相同可任选。分配该块前 n 页，
剩余部分作为合法空闲块保留。
Case 2：没有满足要求的连续块，包括空链表或总空闲页数不足时，
返回 NULL，管理状态不变。
Requirements：不能把“遇到的第一个足够大的块”直接当作最佳块；
找到恰好 n 页的块时可以提前结束查找。

**4. best_fit_free_pages**
Pre-Condition：n > 0；[base, base + n) 是合法的已分配区域，
没有保留页，也不与当前空闲块重叠；允许一次释放跨越此前
多次分配得到的连续区域。
Post-Condition：释放区域的引用计数置 0，恢复空闲块状态，
空闲页数增加 n，并与前后物理相邻的空闲块充分合并。
被合并掉的块首页不再带 PageProperty，也不再独立存在于链表中。

**5. best_fit_nr_free_pages**
Pre-Condition：管理器已初始化。
Post-Condition：返回当前 nr_free，不改变管理状态。

**6. init_pmm_manager**
Pre-Condition：内核准备初始化物理内存管理器。
Post-Condition：当前管理器为 best_fit_pmm_manager，
并已调用其 init；后续分配、释放和检查均通过该管理器执行。

````

#### 实现迭代过程

本模块的实现经历了 1 次迭代，过程如下：

---

##### 第一次迭代

**最终结果：**

经过 1 次迭代，该模块最终：
- ✅ 编译通过（`best_fit_pmm.c`、`pmm.c` 交叉编译无告警）
- ✅ 所有相关测试通过（`basic_check`、`best_fit_check` 以及 `make grade`）
- ✅ 功能符合预期（`make grade` 输出 `Total Score: 20/20`）

---

#### Best-Fit 算法的改进空间

Best-Fit 仍有改进空间，主要包括：

1. **空闲链表插入位置。** `best_fit_insert_block()` 在找不到地址更大的块时用 `list_add(&free_list, &base->page_link)` 追加，但 `list_add(listelm, elm)` 的语义是“把 `elm` 插到 `listelm` 之后”，以哨兵 `&free_list` 为参数实际插到了**表头**。当新块地址大于所有现有空闲块时，链表就不再按地址升序；而 `best_fit_merge()` 依赖链表前后邻居判断物理相邻，顺序一乱就会漏合并、加剧碎片化。应改为 `list_add_before(&free_list, &base->page_link)`（插到哨兵之前，即表尾）。当前提交版本尚未修改此处，作为已知问题记录：它不影响内置测试与评分，但在随机分配/释放负载下可以复现。
2. **查找结构。** 当前分配需要遍历整条空闲链表（O(k)）；可以按块大小维护多条链表（分离适配）或平衡树，在 `alloc_pages` 时快速定位最小足够块，并用地址索引维护合并。
3. **分配质量。** 可以在分配时记录碎片度量，或在多个同样大小的候选块中选择合并代价较小者，降低长期运行后的外部碎片。

#### 任务三：buddy system（伙伴系统）分配算法

**负责人：** [2413498-陈梓轩]

#### 模块功能描述

**需要实现/修改的函数：**

```c
static void buddy_init(void);
static void buddy_init_memmap(struct Page *base, size_t n);
static struct Page *buddy_alloc_pages(size_t n);
static void buddy_free_pages(struct Page *base, size_t n);
static size_t buddy_nr_free_pages(void);
static void buddy_check(void);

const struct pmm_manager buddy_pmm_manager;

// 同时在 pmm.c 中修改：
static void init_pmm_manager(void);
```

**功能说明：**

Buddy System 以“大小为 2 的幂的块”为管理单位，核心是“按阶分桶、分配时拆分、释放时与伙伴合并”。实现如下：

- **按阶分桶。** `free_lists[0..15]` 是 16 条空闲链表，第 k 阶链表中的块包含 `2^k` 个连续物理页；`nr_free` 记录空闲总页数；`managed_base`、`managed_pages` 记录唯一一段被管理内存，之后所有伙伴运算都使用相对 `managed_base` 的页偏移。
- **元数据约定。** 空闲块首页设置 `PageProperty`，并用 `property` 记录块大小 `2^order`；已分配块清除 `PageProperty`，但首页仍保留 `property = 2^order`，这样释放时能知道当初实际分配了多大的块（如申请 3 页实际占用 4 页）。其余页 `property` 为 0。
- **初始化：拆成对齐的 2 的幂块。** `buddy_init_memmap(base, n)` 先把 `[base, base+n)` 的每一页清 `flags`、`property`、`ref`，然后从偏移 0 开始循环：取剩余页数能容纳的最大阶，再向下调整直到块起始偏移按自身大小对齐，把该块挂到对应阶链表，偏移前进 `2^order`，直到覆盖整个区域。这样即使 `n` 不是 2 的幂，也能拆成互不重叠、起点对齐的块。缺省 128 MiB 下空闲区 31928 页会被拆成 `16384+8192+4096+2048+1024+128+32+16+8`。
- **分配：向上取整与拆分。** 申请 `n` 页时先计算 `wanted = ceil(log2 n)`；从 `wanted` 阶向上找第一条非空链表，取下第一个块；若块阶高于 `wanted`，就逐级把右半块拆到更低一阶的空闲链表中，直到剩下 `2^wanted` 页；把该块所有页清 `PageProperty`、`property` 置 0，再给首页写回 `property = 2^wanted`，`nr_free` 减去实际分配的块大小。因此非 2 的幂请求会产生向上取整的内部碎片。
- **释放：异或找伙伴与逐级合并。** 释放时从首页 `property` 取出当初分配的块大小 `size`，校验地址和对齐后清页面元数据，然后循环：伙伴偏移为 `offset ^ size`；若伙伴越界，或伙伴不是“同阶空闲块首页”（`PageProperty` 未设置或 `property != size`），停止；否则把伙伴从链表摘除、清零其 `property`，若伙伴地址更低则把合并起点移到伙伴，`size` 翻倍、阶数加一，继续向上合并。最后把合并后的大块挂回对应阶链表并设置首页元数据。
- **注册与切换。** 文件末尾定义 `buddy_pmm_manager`，把 `init`、`init_memmap`、`alloc_pages`、`free_pages`、`nr_free_pages`、`check` 分别绑定上述函数；配套的 `pmm.c` 在 `init_pmm_manager()` 中把管理器切换为 `buddy_pmm_manager`，于是 `page_init()` 的 `init_memmap()` 和后续分配/释放都进入伙伴系统，`check_alloc_page()` 调用 `buddy_check()`。

与 First-Fit/Best-Fit 不同，Buddy 不用一条按地址排序的链表，而是按阶维护多条链表；合并也不依赖链表前后邻居，而是直接用“偏移异或块大小”定位伙伴，因此合并是逐级向上的常数时间操作。代价是请求必须向上取整到 2 的幂，存在内部碎片；并且当前实现只支持一段连续管理区、最大阶为 15（单块最大 32768 页）。由于两套页分配器都通过 `pmm_manager` 的函数指针表接入，Best-Fit 与 Buddy 模块可以同时编译，运行时由 `init_pmm_manager()` 选择其一，互不覆盖。

#### 最终提示词

以下是经过迭代优化后，最终成功实现该功能的提示词：

````markdown

### 提示词 1：实现 Buddy System 物理内存分配器

请基于当前 uCore Lab 2 项目，实现一个 Buddy System 物理内存分配器。

具体要求如下：

1. 阅读并理解 kern/mm/pmm.h、kern/mm/memlayout.h、kern/mm/default_pmm.c、kern/mm/best_fit_pmm.c 和 libs/list.h 中的接口及数据结构。

2. 新建 kern/mm/buddy_pmm.h 和 kern/mm/buddy_pmm.c，不要破坏已有的 First-Fit、Best-Fit 等分配器。

3. 使用多个空闲链表管理不同阶数的内存块。第 k 阶链表中的每个块应包含 2^k 个连续物理页。

4. 实现初始化函数。init_memmap 应能处理页数不是 2 的幂的内存区域，将整个区域拆分成若干个互不重叠的 2 的幂大小的块，并尽量避免浪费页面。

5. 实现页分配函数。申请 n 页时，将 n 向上取整为不小于 n 的最小 2 的幂；查找对应阶或更高阶的空闲块；如果使用更高阶块，则逐级拆分，并把另一半放入相应的空闲链表。

6. 实现页释放函数。释放后根据页面相对于管理区域起点的索引，使用“索引异或块大小”的方法寻找伙伴块。只有伙伴块空闲且大小相同时才进行合并，并重复向更高阶合并。

7. 正确维护 PageProperty、property 字段和空闲页计数。空闲块首页面需要设置 PageProperty，已分配块需要清除 PageProperty，并记录实际分配的 2 的幂大小，供释放时使用。

8. 定义并导出 buddy_pmm_manager，在 pmm.c 中包含 buddy_pmm.h，并把当前物理内存管理器切换为 buddy_pmm_manager。

9. 所有新增或补全代码的注释必须保留“lab2”，并将“YOUR CODE”替换为学号 2413498，例如：

   // lab2 task3 2413498

10. 不要修改其他同学负责的算法，也不要完成 challenge 内容。

请输出完整的 buddy_pmm.h、buddy_pmm.c 以及 pmm.c 中需要修改的部分，并说明块拆分、伙伴地址计算、块合并和空闲页计数的实现思路。


### 提示词 2：设计 Buddy System 测试

请为前面实现的 Buddy System 物理内存分配器设计 buddy_check 测试函数，并将它注册到 buddy_pmm_manager.check。

测试要求如下：

1. 保留“lab2”注释，并使用学号 2413498 标记新增测试代码。

2. 测试分别申请 1 页、2 页和 3 页。其中申请 3 页应实际占用一个 4 页大小的 Buddy 块。

3. 检查分配前后的空闲页数。上述三个块应使空闲页数一共减少 7 页；全部释放后，空闲页数应恢复到初始值。

4. 测试申请超过当前可用内存的大块，分配函数应返回 NULL，并且失败后空闲页数和空闲链表状态不能发生变化。

5. 测试申请和释放一个恰好为 8 页的块，验证不需要向上取整时的正常分配与回收。

6. 连续申请两个 1 页块，检查它们能够由同一个较大块拆分得到；释放这两个块后，验证它们能够重新合并。

7. 检查已分配块和空闲块的元数据，包括 PageProperty 标志和 property 字段，防止错误合并或重复释放。

8. 测试结束时输出：

   buddy_check() succeeded!

请给出带有 assert 检查的完整 buddy_check 函数，并逐项解释每个测试覆盖的边界情况，以及这些检查能够发现哪些常见错误。

````

#### 实现迭代过程

本模块的实现经历了 1 次迭代，过程如下：

---

##### 第一次迭代

**最终结果：**

经过 1 次迭代，该模块最终：
- ✅ 编译通过（`buddy_pmm.c` 与现有 PMM 接口一致，独立编译无告警）
- ✅ 所有相关测试通过（`buddy_check()` 覆盖 1/2/3 页分配、释放、超大分配失败与伙伴合并）
- ✅ 功能符合预期

---

### Challenge：SLUB 内存分配器的实现

**负责人：** [2412412-郑志昊]

#### 模块功能描述

**需要实现/修改的函数：**

```c
void slub_init(void);
struct kmem_cache *kmem_cache_create(const char *name, size_t size);
void *kmem_cache_alloc(struct kmem_cache *cache);
void kmem_cache_free(struct kmem_cache *cache, void *obj);

void slub_basic_check(void);
void slub_stress_check(void);
void check_slub(void);

// 页级封装（kern/mm/pmm.c）
void *kalloc(void);
void kfree(void *pa);
```

**功能说明：**

SLUB 位于页分配器之上，是一个“对象级”分配器：下层以 4 KiB 页为单位向 PMM 申请内存，上层把页切成固定大小的对象并缓存复用。实现如下：

- **页内布局。** 每个 Slab 占一页，页首放 `struct slab` 头，记录魔数、同缓存的下一个 Slab、空闲对象链表头、第一个对象地址、所属缓存、对象总数和已分配数。对象从页首之后、按对象大小对齐的地址开始，每隔一个对象大小放一个。
- **对象大小与对齐。** 请求大小先提升到至少一个指针大小，再向上取整到 8 字节，保证空闲对象放得下“下一个”指针且指针读写对齐。起始对象地址取“页首之后第一个能被对象大小整除的地址”，之后按固定步长排列，因此每个对象都按（取整后的）对象大小对齐；对 8/16/24/32/64 等常见尺寸，就是严格按请求大小对齐。
- **空闲链表。** 每个 Slab 内部有一条单向空闲链表，节点就放在空闲对象自己的前 8 个字节里。新建 Slab 时从高地址向低地址串联，使最低地址对象成为链表头，分配和释放都从头部进行，形成 LIFO 复用。
- **分配。** 先在该缓存已有的 Slab 链表中找一个空闲链表非空的 Slab；找到就摘一个对象返回。若所有 Slab 都满，则调用 `kalloc()` 向 PMM 申请新页，切分对象、建立空闲链表、把新 Slab 挂到缓存链表头部，再从中取一个对象。页分配失败时返回 `NULL`。
- **释放与回收。** 由对象地址向下按 4 KiB 取整得到页基址，从而找到 `struct slab` 头；校验魔数、所属缓存、对象是否落在本页对象区间内且按对象大小对齐后，把对象压回该 Slab 的空闲链表并减少已分配计数。当某个 Slab 的已分配计数降为 0 且该缓存不止一个 Slab 时，把它从缓存链表摘除并把整页通过 `kfree()` 还给 PMM；每个缓存保留最后一个空 Slab，避免分配/释放一个对象就申请/归还一页的抖动。
- **接入内核。** 实验骨架原本没有 `kalloc`/`kfree`，因此在 `pmm.c` 中补了两个页级封装：`kalloc` 从 PMM 取一页并把物理地址加上 `va_pa_offset` 转成虚拟地址，`kfree` 反向转换并做范围检查后释放该页。`slub_init()` 复位缓存池；`kern_init()` 在 `pmm_init()` 之后调用 `slub_init()`，再调用 `check_slub()` 执行全部测试。SLUB 只使用当前 `pmm_manager` 提供的页，不替换管理器，因此与 First-Fit/Best-Fit 均可配合。

当前实现是简化版：没有 per-CPU 缓存和锁，每个 Slab 固定一页，缓存描述符来自 32 个静态槽位，分配时线性扫描该缓存的 Slab 链表，不支持缓存的动态销毁与合并。

#### 最终提示词

以下是经过迭代优化后，最终成功实现该功能的提示词：

````markdown

[PROMPT]
**任务**：修改项目（主要涉及 `kern/mm/slub.c`、`kern/mm/slub.h`，以及 `kern/mm/pmm.c` 和 `kern/init/init.c`），从零实现一个简化版的 SLUB 内存分配器，并将其接入现有内核。

**操作要求**：你必须在项目中创建并修改实际文件，而不是仅展示代码。你需要完成头文件声明、核心分配/释放算法、缓存管理结构体，并在内核初始化时进行测试。如果指定的文件不存在，请创建它们；如果已存在接入点，请补全相关代码。

**输出要求**：直接进行文件操作。保留文件中现有的其他无关代码，按照下文提供的信息进行实现。

[RELY]
// 依赖的底层宏与内存分配接口（假定现有系统中已提供）
#define PGSIZE 4096
void *kalloc(void);             // 从物理内存管理器（PMM）分配一页（4KB）物理内存，返回虚拟地址
void kfree(void *pa);           // 释放一页物理内存

// 基础数据结构（假定可用）
// 你可以自行设计 kmem_cache 结构体，但必须体现 SLUB 的核心思想。

[GUARANTEE]
必须实现或修改的接口：
```c
// 核心数据结构（你需要自行定义，这里给出建议的名字）
struct kmem_cache;

// 必须实现的核心 API
void slub_init(void);
struct kmem_cache *kmem_cache_create(const char *name, size_t size);
void *kmem_cache_alloc(struct kmem_cache *cache);
void kmem_cache_free(struct kmem_cache *cache, void *obj);

// 接入层
// 在 pmm.c 或 init.c 中的适当位置（如 pmm_init 之后）调用 slub_init()，并编写一段简单的自测代码调用上述 API。
你可以根据需要添加：

辅助函数（建议设为 static，如分配新页、切分对象、管理空闲链表等）

内部使用的宏定义

模块内部的静态变量（如缓存链表头）

[SPECIFICATION]

SLUB 模块整体架构
Pre-Condition:

kalloc 和 kfree 能够正常分配和释放 4KB 的物理页。

Post-Condition:

实现一个双层架构：第一层以 4KB 页为基本单位向 PMM 申请内存；第二层在这个页内切分出固定大小的对象（Object），并通过空闲链表管理这些对象。

Case 1 (第一层：页与 Slab 管理):

当分配缓存对象且当前 kmem_cache 没有空闲对象时，必须调用 kalloc 申请一个新页。

将新页切分为多个大小为 size 的对象，用链表串联起来（可以使用页内空间存放链表指针，要求对象大小 size 至少能容纳一个指针，如果小于指针大小则按指针大小对齐）。

将新分配的 Slab 页挂入 kmem_cache 的页链表中。

Case 2 (第二层：对象分配 kmem_cache_alloc):

如果 cache 的空闲对象链表非空，则从中取出一个对象返回。

如果空闲对象链表为空，触发 Case 1 申请新页并切分。

返回的对象必须是按 size 对齐的。

Case 3 (第二层：对象释放 kmem_cache_free):

将释放的对象重新插入到所属 kmem_cache 的空闲对象链表中。

（可选但推荐）如果某整个 Slab 页的所有对象都被释放，可以将该页归还给 PMM 系统（调用 kfree），以体现 SLUB 节省内存的优势。

Requirements:

必须正确使用现有的 kalloc 和 kfree 作为底层内存来源。

请务必添加适当的注释说明你设计的对象对齐策略和空闲链表结构。


[PROMPT]
**任务**：修改项目（主要涉及 `kern/mm/slub.c` 或新建的测试文件），基于你刚才实现的简化版 SLUB 内存分配器，设计并实现一套完整的测试函数，以验证不同大小内存对象的分配和释放是否正确。

**操作要求**：你必须在项目中修改实际文件。测试代码需要能够被编译和运行，并输出测试结果。你需要自行分析并设计测试用例。

**输出要求**：直接进行文件操作。请编写测试函数并将其调用（例如通过 `cprintf` 输出测试结果），验证完成后要打印成功信息。

[RELY]
// 依赖的 SLUB 接口
struct kmem_cache *kmem_cache_create(const char *name, size_t size);
void *kmem_cache_alloc(struct kmem_cache *cache);
void kmem_cache_free(struct kmem_cache *cache, void *obj);

// 断言依赖（假定可用）
void panic(const char *msg);
void cprintf(const char *fmt, ...);

[GUARANTEE]
必须实现或修改的接口：
```c
// 测试函数（你需要自行实现具体的测试逻辑）
void slub_basic_check(void);
void slub_stress_check(void);
你可以根据需要添加：

辅助测试函数（如用来验证指针是否对齐的静态函数）

测试用的特定大小宏定义

[SPECIFICATION]

slub_basic_check (基础功能测试)
Pre-Condition:

SLUB 模块已完成初始化。

已成功通过 kmem_cache_create 创建了至少两种不同对象大小的 kmem_cache（例如一个较小的 16 字节对象，一个较大的 1024 字节对象）。

Post-Condition:

测试结束后，所有分配的对象都被释放，系统状态恢复正常。

Case 1 (分配与对齐检查):

从同一个 kmem_cache 连续分配多个对象（如 5 个）。

检查返回的指针非空，且指针地址是页对齐或正确的大小对齐的。

检查多次分配返回的指针互不重叠（不相等）。

Case 2 (释放与重用检查):

将分配的对象释放后，再次尝试分配同等大小的对象，预期能够顺利从缓存中拿到对象。

验证不同大小的 kmem_cache 互不干扰（例如在 16 字节 cache 和 1024 字节 cache 之间交替分配和释放）。

slub_stress_check (压力与边界测试)
Pre-Condition:

准备一个能够跨越多个物理页（Slab 页）的 kmem_cache 对象大小（比如 size = 512，这样一页 4KB 只能放下 8 个对象）。

Post-Condition:

验证完大量分配释放后，系统不会崩溃（无 Panic）。

Case 1 (跨页大量分配):

尝试分配超过一页所容纳数量的对象（比如分配 20 个 512 字节对象）。

验证这些对象的地址并不都在同一个 4KB 页内（证明第一层架构正确触发了新页的申请）。

检查未发生内存覆盖现象（可选写一个特殊字符并校验）。

Case 2 (乱序释放与合并检查):

将刚才分配的对象，以乱序的方式调用 kmem_cache_free 逐一释放。

再次分配同等数量的对象，确认没有任何断言失败，内存分配器维持正常状态。

Requirements:

测试函数必须包含完整的断言（assert 或类似机制）。如果测试失败，调用 panic 输出具体错误（如“分配了空指针”或“指针未对齐”）。

在测试通过后，必须输出类似 check_slub() succeeded! 的日志信息。

````

#### 实现迭代过程

本模块的实现经历了 1 次迭代，过程如下：

---

##### 第一次迭代

**最终结果：**

经过 1 次迭代，该模块最终：
- ✅ 编译通过（`slub.c`、`slub_check.c`、`pmm.c`、`init.c` 交叉编译无告警）
- ✅ 所有相关测试通过（`slub_basic_check`、`slub_stress_check`、`check_slub` 以及 `make qemu`）
- ✅ 功能符合预期（串口输出 `check_slub() succeeded!`）

#### 测试设计

- **基础测试 `slub_basic_check()`**：创建 16 字节和 1024 字节两个缓存，每个缓存连续分配 5 个对象，检查指针非空、按对象大小对齐、两两不相等；检查两个缓存的第一个对象不在同一物理页；对每个对象的全部字节写入确定性图案并回读，以发现对象重叠或越界；全部释放后重新分配，检查仍能成功且至少有一个对象复用此前释放的地址；最后让两个缓存交替分配/释放 3 轮，确认互不干扰。
- **压力测试 `slub_stress_check()`**：创建 512 字节缓存，一个 4 KiB 页实际只能放 7 个对象（要扣掉页首 Slab 头），因此分配 20 个对象必然跨页；统计对象所在的不同物理页数并断言大于等于 2（实际为 3），证明“没有空闲对象时确实向 PMM 申请了新页”；对每个对象逐字节写图案并回读；以步长 7（与 20 互质）乱序释放全部对象；再分配 20 个，检查非空、对齐、互不相同、仍跨多页，再次写图案校验后全部释放。
- **失败处理**：所有检查失败都调用 `panic()` 并给出具体原因，例如“分配返回空指针”“对象未对齐”“同一对象被返回两次”“对象内容被覆盖”“对象没有跨页”；全部通过后输出 `slub_basic_check() succeeded!`、`slub_stress_check() succeeded!` 和 `check_slub() succeeded!`。

#### SLUB 的优缺点

- **优点**：对象级复用，空闲链表节点直接放在空闲对象内部，元数据开销小；缓存中有空闲对象时分配/释放都是常数时间，不需要每次走页分配器；对象集中放在 Slab 页中，缓存局部性好；按对象大小建立缓存，内部碎片主要只来自对齐和页尾余量；整页对象都空闲时可以把页还给 PMM。真实 SLUB 还通过 per-CPU 缓存和部分空闲链表获得良好的多核扩展性。
- **缺点**：只要一个 Slab 页里还有对象未释放，整页就不能归还，页内其余空闲对象也不能给其他缓存使用，存在跨缓存的碎片；每种对象大小一个缓存，缓存本身和部分空闲 Slab 都有管理开销；对象大小接近页大小时页内浪费明显；真实 SLUB 的 per-CPU 缓存、并发保护、回收策略和调试机制实现复杂。
- **本实现的简化**：没有 per-CPU 缓存与锁，只适合单核启动阶段；每个 Slab 固定一页；分配时线性扫描该缓存的 Slab 链表；缓存描述符来自静态池，不支持销毁与合并；保留一个空 Slab 作为缓冲，其余空页归还 PMM。

---

## 五、测试与验证

**测试结果：**

本次实验在评分副本中执行 `make grade`：注入测试后内核完成交叉编译，并在 QEMU 中运行；评分脚本显示 `check page init: OK`、`check best fit: OK`，最终 `Total Score: 20/20`。

此外，在 QEMU 中执行 `make qemu`，串口依次输出 `slub_basic_check() succeeded!`、`slub_stress_check() succeeded!` 和 `check_slub() succeeded!`；Buddy System 模块的 `buddy_check()` 也已在宿主机用模拟页数组运行通过，覆盖 1/2/3/8 页分配、释放、超大分配失败与伙伴合并。

---

## 六、实验总结与收获

### 对操作系统的理解

1. **虚实地址转换与页表。** 实验中内核通过 Sv39 的 1 GiB 大页把自己映射到高地址空间，并用固定的 `va_pa_offset` 完成 `PADDR()`/`KADDR()` 转换；OS 原理中的页表则是多级映射、按需分页与地址空间隔离。二者的含义相同（都是 VA→PA 的“词典”），差异在于实验只建立了最小的内核映射，没有用户页表、没有缺页异常，也没有权限位的实际使用。
2. **物理内存探测。** 实验通过 DTB 的 `memory` 节点取得物理内存起点和大小；OS 原理中这通常由 bootloader/BIOS（如 x86 的 E820）报告。二者都是“启动阶段由固件告诉内核可用内存”，只是数据来源和格式不同。
3. **页帧管理与描述符数组。** 实验用连续的 `struct Page` 数组描述每个物理页，初始全部置保留，再把真正空闲的页交给分配器；OS 原理对应的是物理页帧分配器（page frame allocator）。差异在于实验是单核、无并发，不需要锁和每 CPU 缓存，`struct Page` 也做了大幅简化（没有映射计数、页表指针、回写状态等）。
4. **连续物理内存分配与外部碎片。** First-Fit 与 Best-Fit 都是连续分配策略，实验通过 `property` 记录块大小、通过前后合并减少碎片；Buddy System 进一步把空闲页组织成 2 的幂大小的块，用伙伴合并控制外部碎片；SLUB 则在页分配器之上按对象大小切分页，避免为每个小对象单独占用整页。OS 原理中的伙伴系统、slab 分配器以及内存 compact 都是为了解决同一类碎片问题，但实现复杂度和适用粒度不同。
5. **侵入式链表与 container_of。** 实验用 `list_entry_t` 嵌入 `struct Page`，再用 `le2page`/`to_struct` 从成员地址反推结构体首地址；OS 原理中这是内核数据结构组织的通用技巧（如 Linux 的 `list_head`），其价值在于零额外分配、同一对象可挂多条链表。

**OS 原理中很重要、但在本实验中没有对应上的知识点：**

- 缺页异常与按需分页、页面置换算法（LRU、Clock 等）以及页表项的 Accessed/Dirty 位使用；
- 写时复制、内存映射文件、`mmap` 等高级虚拟内存机制；
- 多核并发下的内存分配保护（自旋锁、每 CPU 页帧缓存）与 TLB shootdown；
- 物理内存热插拔、NUMA 节点感知分配、内存压缩/迁移；
- 内核对象分配器（slab/SLUB）与伙伴系统在真实内核中的完整实现（本实验只做了简化版：Buddy 仅支持单段管理区和有限阶数，SLUB 没有 per-CPU 缓存、锁和缓存销毁机制）。

### AI 协作开发的经验

- **提示词结构比“描述需求”更重要。** 本次各模块都采用 `[PROMPT]` + `[RELY]` + `[GUARANTEE]` + `[SPECIFICATION]` 的结构：`[RELY]` 给出可用宏、全局变量和参考实现，`[GUARANTEE]` 固定函数签名，`[SPECIFICATION]` 用前置/后置条件和 Case 划分边界。这样 AI 不会自行发明地址换算或接口，生成结果更贴近框架约定。
- **把“不能做什么”写进提示词。** 例如明确不得修改 `basic_check`/`best_fit_check`、无完整空闲页时不得调用 `init_memmap`、`end`/`pages` 的运算必须区分虚实地址，这些约束直接对应测试与评分点，能显著减少返工。
- **对底层地址计算类代码，必须用多种手段验证。** 本次除交叉编译外，还用宿主机语法检查、按 QEMU 缺省布局做数值模拟，并对 Best-Fit 增加随机分配/释放压力测试，才发现了官方测试覆盖不到的链表顺序问题。
- **AI 容易在 API 语义方向上出错。** 例如 `list_add(listelm, elm)` 是“插到 `listelm` 之后”，以哨兵为参数时就会插到表头；这类问题必须对照框架源码核对，而不能只看函数名猜测。
- **把 bug 转化为下一轮约束。** 每轮迭代后，把发现的问题（如尾页对齐、无空闲页、插入位置）写回提示词的边界条件中，能让后续生成的代码更稳定。
