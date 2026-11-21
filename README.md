# wasm_learn

Learning records for [Tiny-Wasm-Compiler-Learn](https://github.com/sky-co/Tiny-Wasm-Compiler-Learn)
For reference, refer to [Compiler-Learn-Nodes](https://github.com/sky-co/Compiler-Learn-Nodes)

## ARM64 Tests

Requirements: Bash, `aarch64-linux-gnu-g++`, `qemu-aarch64`, `wast2json`, ARM64 runtime libraries, and GoogleTest sources.

Run the shared [test script](run_arm64_test.sh) from the repository root:

```shell
./run_arm64_test.sh Chapter02 local.wast
./run_arm64_test.sh Chapter03 arithmetic.wast
```

Arguments are `ChapterNN input.wast`. The WAST path is relative to the chapter directory. The script generates `test.json`, `chapterNN_main`, and `wasm_chapterNN_test`. Each chapter provides `chapterNN_main.cpp` and `wasm_chapterNN_test.cpp`; both programs read the generated JSON path from `WASM_TEST_JSON`. When run manually without that variable, existing chapter-specific JSON defaults are preserved.

Generated fixtures and binaries stay under `build/chapterNN-arm64-test`. Supported environment overrides are `BUILD_DIR`, `GTEST_SOURCE_DIR`, `AARCH64_CXX`, `QEMU_AARCH64`, and `AARCH64_SYSROOT`. Without an override, GoogleTest sources are searched for in `../googletest` and then `/usr/src/googletest`.

The same tests are registered with CTest:

```shell
cmake -S . -B build
ctest --test-dir build --output-on-failure -R '^chapter0[23]_arm64_gtest$'
```

For another chapter following the same source naming convention, reuse this script and add its CTest registration in [CMakeLists.txt](CMakeLists.txt); no chapter-specific shell script is needed.

Promote understanding words

```text
请帮我解析本章的学习目标和分解需要完成的任务，并提供建议、工具和最佳实践
```

## Block points
- 如何从wasm源码映射到ARM64汇编
- 如何高效debug（Emit了机器码后，如何定位问题）
- 细节（如字节对齐， 控制流处理）
- Learning和实际工程的差异与联系

## Browser toolbox

Requirements: Node.js >= 18.17, WABT, LLVM, AArch64 binutils, and a modern browser.
On Ubuntu/Debian, install the command-line dependencies from the repository root:

```shell
bash tooling/install_dependencies.sh
```

Use `bash tooling/install_dependencies.sh --check` to verify without installing.
Then run `node tooling/server.mjs` and open <http://127.0.0.1:1818/wasm_interpreter.html>
for WAST conversion, Wasm disassembly, AArch64 assembly/disassembly, and binary-to-hex conversion.

In **5. AArch64 Binary to Hex**, enter one `0` or `1` in each of the 32 boxes,
ordered from bit 31 (most significant) to bit 0 (least significant), matching the ARM encoding diagrams.
Click **translate** to show one hexadecimal digit per four bits and the complete
8-digit instruction word below, for example `0xD10043FF`.
Click **reset** next to **translate** to fill all 32 boxes with `0`, reset all eight
hexadecimal digits to `0`, clear validation errors, and immediately display `0x00000000`.
This conversion runs entirely in the browser and also works when opening
[wasm_interpreter.html](tooling/wasm_interpreter.html) directly, without the local service.

See the [toolbox guide](tooling/README.md) for dependencies and usage.
