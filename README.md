#wasm_learn

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
