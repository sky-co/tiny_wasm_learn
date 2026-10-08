#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

// $ aarch64-linux-gnu-gcc -Wall -Wextra -O0 -g mmap_main.c -o mmap
// $ qemu-aarch64 -L /usr/aarch64-linux-gnu ./mmap

// $ qemu-aarch64 -g 1234 -L /usr/aarch64-linux-gnu ./mmap
// vscode debug
/*
可以用 **“查看函数指针地址上的指令 + 单步进入 + 确认返回”** 来验证。

在 `func();` 行设断点，用 VS Code 启动调试并停住后，在 Debug Console 中执行：

```text
-exec p/x func
-exec x/4xb func
-exec x/i func
```

预期看到 `func` 指向一段内存，字节为 `c0 03 5f d6`，反汇编结果为 `ret`。这证明该地址存放的是 AArch64 的 `RET` 指令。

接着按 **F11（单步进入）**，观察寄存器 `pc`；当 `pc` 等于 `func` 的地址时，执行：

```text
-exec info registers pc
-exec x/i $pc
``

此时应显示 `pc` 指向该地址，当前指令是 `ret`。再单步执行一次，程序应返回到 `func()` 调用之后；继续运行并停在后面的 `printf` 行，也能确认调用正常返回。
注意：仅看到输出文本不能单独证明执行了那段机器码；检查 `pc` 和反汇编结果才是直接证据。

原因是 **F11 执行源码级单步**，而 `func` 指向 `mmap` 出来的匿名内存，没有 DWARF 源码行号；GDB 会直接跳到下一条有源码位置的语句。这里的机器码又只有一条
`RET`，所以源码视图里看起来像是跳过了函数。

我用当前程序复现了：在 `func();` 行暂停后，GDB 显示 `func` 指向 `0x400000a4e000`，该地址反汇编为 `ret`；对它执行 `stepi` 后，PC 返回 `main`，停在
`printf` 行。这说明机器码确实执行了，只是 F11 不会逐条显示无源码映射的指令。

在 VS Code Debug Console 可这样验证：

```text
-exec p/x func
-exec tbreak *func
-exec continue
-exec x/i $pc
-exec stepi
-exec info registers pc
```

临时断点会停在 `func` 的第一条指令 `ret` 上；`stepi` 执行它后，PC 应回到调用者。`launch.json` 中的 `exec-continue`
不是原因，它只是在调试器连接后继续运行。

*/

int main() {
    // 将文件映射到内存
    long page_size = sysconf(_SC_PAGESIZE);
    // 1. 申请具有 PROT_READ | PROT_WRITE  权限的匿名内存
    void *mem = mmap(NULL, (size_t)page_size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (mem == MAP_FAILED) {
        perror("mmap failed");
        return 1;
    }

    // RET -> C0035FD6
    unsigned char opcode[] = {0xC0, 0x03, 0x5F, 0xD6};

    memcpy(mem, opcode, sizeof(opcode));

    if (mprotect(mem, page_size, PROT_READ | PROT_EXEC) == -1) {
        perror("mprotect failed");
        munmap(mem, (size_t)page_size);
        return 1;
    }

    void (*func)() = (void (*)())(mem);
    func();
    printf("Executed machine code successfully! \n");

    munmap(mem, (size_t)page_size);
    return 0;
}
