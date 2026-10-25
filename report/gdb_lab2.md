# 分支任务：GDB 调试页表查询过程

负责人：2413498 陈梓轩

## 1. 调试目标

使用两个 GDB 调试器观察 uCore 在 QEMU 中运行时，从 TLB 未命中到 Sv39 页表查询，再到写入 QEMU TLB 的完整过程。

## 2. 调试环境

- Ubuntu 22.04
- QEMU 6.2.0
- GDB 12.1
- RISC-V 64
- uCore Lab 2
- 物理内存管理器：Buddy System

系统自带的 /usr/bin/qemu-system-riscv64 已被 stripped，无法进行源码级调试，因此单独编译了带调试信息的 QEMU 6.2.0，并且只构建 riscv64-softmmu。

QEMU 调试版路径：

/home/dick/qemu-6.2.0/build-debug/qemu-system-riscv64

## 3. 三个终端的用途

终端一：使用宿主机 GDB 调试 QEMU 源码。

终端二：使用 gdb-multiarch 通过 localhost:1234 调试 uCore 内核。

终端三：查看 QEMU 源码并记录调试数据。

## 4. TLB 未命中

在 QEMU 源码函数 riscv_cpu_tlb_fill 上设置断点，并且只在 satp.MODE 等于 8 时停止。MODE 等于 8 表示当前使用 Sv39 地址转换模式。

断点命中时，本次访问类型为 MMU_INST_FETCH，即取指访问。

待转换虚拟地址为：

0xffffffffc02000d8

satp 中记录的根页表物理地址为：

0x80204000

Sv39 的参数为：

- 页表级数：3
- 每级虚拟页号索引：9 位
- 每个页表项：8 字节

## 5. Sv39 页表查询

QEMU 在 get_physical_address 函数中遍历页表。第一次查询得到：

- 查询轮次：0
- VPN[2]：0x1ff
- 页表项地址：0x80204ff8
- 页表项内容：0x200000cf
- 物理页号 PPN：0x80000

页表项低位为 0xcf，其中 V、R、W、X、A、D 标志有效。由于 R、W、X 不全为零，该页表项是叶子页表项，而不是指向下一级页表的非叶子项。

该叶子项位于 Sv39 的最高级页表，因此它表示一个 1 GiB 大页映射。当前 Lab 2 的启动页表不需要继续访问后两级页表。

物理页基址为：

0x80000000

虚拟地址在 1 GiB 大页中的偏移为：

0x2000d8

所以最终物理地址为：

0x80000000 + 0x2000d8 = 0x802000d8

最终地址转换关系为：

0xffffffffc02000d8 -> 0x802000d8

## 6. 写入 QEMU TLB

页表查询成功后，QEMU 在 cpu_helper.c 的 tlb_set_page 调用处写入 TLB。

观察到的参数为：

- 虚拟地址：0xffffffffc02000d8
- 物理地址：0x802000d8
- 访问权限 prot：7
- MMU 索引：1
- TLB 项大小：0x1000，即 4 KiB
- 写入 TLB 的虚拟页：0xffffffffc0200000
- 写入 TLB 的物理页：0x80200000

prot 等于 7，表示该映射具有读、写、执行权限。

虽然页表中的叶子项描述的是 1 GiB 大页映射，但本次写入 QEMU TLB 的缓存粒度是 4 KiB。

## 7. 完整流程

本次调试观察到的完整流程为：

1. CPU 访问虚拟地址，QEMU TLB 中没有对应映射。
2. QEMU 进入 riscv_cpu_tlb_fill。
3. 根据 satp 确认地址转换模式为 Sv39。
4. 从 satp 提取根页表物理地址 0x80204000。
5. 使用 VPN[2] 等于 0x1ff 定位页表项 0x80204ff8。
6. 读取 PTE 0x200000cf，判断它是最高级叶子项。
7. 根据 1 GiB 大页规则组合 PPN 和页内偏移。
8. 得到物理地址 0x802000d8。
9. 调用 tlb_set_page，把页对齐后的映射写入 QEMU TLB。
10. 后续对同一页面的访问可以直接命中 TLB，无需再次查询页表。

## 8. 总结

TLB 是虚拟页号到物理页号映射的高速缓存。TLB 未命中时，QEMU 模拟 RISC-V MMU，根据 satp 中的模式和根页表地址完成 Sv39 页表查询。

Sv39 最多进行三级页表访问，但允许在任意级遇到合法叶子项后提前结束。本实验的内核启动映射使用最高级叶子项，因此形成 1 GiB 大页，只查询一次 PTE 即可完成虚拟地址到物理地址的转换。
