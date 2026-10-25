# Lab 2 提示词汇总

## 功能模块一：任务一 物理内存探测与页描述符初始化

负责人：[2412412郑志昊]

### 提示词 1：实现 page_init

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
page_init

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


## 功能模块二：任务二 Best-Fit 连续物理内存分配

负责人：[2412528赵甫文]

### 提示词 1：实现并接入 Best-Fit

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


## 任务三：Buddy System

负责人：2413498 陈梓轩

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


## Challenge：SLUB 内存分配器

负责人：[2412412 郑志昊]

### 提示词 1：实现简化版 SLUB 内存分配器

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


### 提示词 2：设计 SLUB 测试

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
