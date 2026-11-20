# WebAssembly / AArch64 工具箱

## 软件依赖

| 依赖 | 用途 | Ubuntu/Debian 软件包 |
| --- | --- | --- |
| Bash | 执行安装脚本 | `bash`（通常已预装） |
| Node.js >= 18.17 | 运行本机 HTTP 服务和测试 | `nodejs` |
| WABT | 提供 `wast2json`、`wasm-objdump` | `wabt` |
| LLVM（包含 AArch64 后端） | 提供 `llvm-mc` 汇编器 | `llvm` |
| AArch64 binutils | 提供 `aarch64-linux-gnu-objdump` | `binutils-aarch64-linux-gnu` |
| 现代浏览器 | 页面操作与 HTTP 请求 | Chrome / Edge / Firefox 等，自行安装 |

推荐使用仍在维护的 Node.js LTS。所有命令必须在启动服务的终端 PATH 中可用。
不需要 npm 包、Python、QEMU、ARM64 硬件或 `xxd`；
服务直接写入机器码字节，因此无需额外安装十六进制转换工具。

### 一键安装（Ubuntu/Debian）

在项目根目录执行：

```sh
bash tooling/install_dependencies.sh
```

脚本按需安装缺失的软件包，已有满足版本要求的依赖不会触发 apt，
可以重复运行。发生安装时会执行 `apt-get update` 和
`apt-get install -y --no-install-recommends ...`，需要网络连接及管理员权限；
这会安装所列软件包和必要的间接依赖，不会执行全系统升级。
非 root 用户通过 `sudo -A` 获取权限，密码由 askpass 界面处理；
若系统未配置 askpass（`SUDO_ASKPASS`），请配置系统的 askpass 程序，
或由管理员在 root 终端运行脚本，不要把密码写入脚本。

安装支持提供上述软件包的 apt 系统，例如 Ubuntu 24.04+、Debian 13+。
旧发行版默认源中的 Node.js 可能低于 18.17，脚本会在安装后明确报错，
此时需要更新发行版/受信软件源，或自行安装满足版本的 Node.js。
脚本不添加第三方软件源、不下载执行远程安装脚本，也不修改 PATH 或 shell 配置；
如果旧版 Node.js 覆盖了 apt 安装的版本，请先调整 PATH。
其他操作系统需要自行安装对应依赖，不支持自动安装。

仅检查依赖和版本，不联网、不安装、不需要管理员权限：

```sh
bash tooling/install_dependencies.sh --check
```

检查会显示每个命令的版本和实际路径，缺失、命令无法运行或 Node.js 版本过低时，
脚本以非零状态退出。安装完成后也会自动执行相同的检查。

## 启动

在项目根目录运行：

```sh
node tooling/server.mjs
```

浏览器打开 <http://127.0.0.1:1818/wasm_interpreter.html>。
可以使用 `PORT=8081 node tooling/server.mjs` 更换端口，Ctrl+C 停止服务。
HTML 不能通过 `file://` 直接调用本机命令，必须通过该服务打开。

## 使用

1. 输入绝对路径、相对于项目根目录的路径（例如 `Chapter02/local.wast`），
   或 HTTP(S) URL，点击 **wast2json**。本地转换在源文件同目录写入或覆盖
   同名 `.json` 和编号 `.wasm` 文件；远程文件及产物保存在临时目录，
   服务正常退出时清理。二进制 `.wasm` 无需转换，直接列为可用模块。
2. 选择生成的模块（支持一个 WAST 包含多个模块），或输入独立 Wasm 路径/URL，
   点击 **wasm-objdump**，显示 `wasm-objdump -d -x` 的输出。
3. **Disassembler** 接受 `0xD10043FFU` 等 32 位十六进制指令数值，
   显示小端字节 `FF 43 00 D1`，再调用 AArch64 objdump。
   每次请求使用独立临时二进制文件，不共享 `/tmp/ret.bin`。
4. **Assembler** 接受单条 AArch64 指令，例如 `sub sp, sp, #0x10`，
   使用 `llvm-mc -triple=aarch64 -show-encoding`，显示小端字节和
   `0xD10043FFU` 格式的指令数值。不支持伪指令、多条指令及未解析的符号。

服务仅监听 `127.0.0.1`，校验 Host 和同源请求，不启用 CORS。
它可以读取指定的本地 Wasm/WAST 文件并生成文件，请勿代理到公网，
也不要下载或处理不可信的模块。命令调用不经过 shell；
命令超时 30 秒，输出上限 4 MiB；源文件上限 16 MiB，下载超时 15 秒。
错误及命令诊断会显示在对应模块的输出区。

## 验证

安装上述工具后执行（使用临时目录，不修改章节样例）：

```sh
node --test tooling/server.test.mjs tooling/install_dependencies.test.mjs
```

测试覆盖 WAST 多模块转换、本地/远程文件反汇编、
AArch64 大小端往返、无效输入、已移除接口及同源校验。
安装脚本测试使用模拟命令，覆盖按需安装、版本检查及失败处理，不会真正调用 apt/sudo。
