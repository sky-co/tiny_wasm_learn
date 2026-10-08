add.wat
   ↓
wat2wasm
   ↓
读取 .wasm 字节
   ↓
解析 section 和 opcode
   ↓
生成 Arm64 机器码
   ↓
写入可执行内存
   ↓
调用生成的函数
   ↓
测试、格式检查、CI

## 执行swq 检查

### Code format
```shell
find -regex '.*\.\(cpp\|hpp\|c\|h\)' -print0 |
  xargs -0 clang-format-19 -style=file --Werror --dry-run
```

```shell
find /home/q491938/work_space/wasm_learn -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.c' -o -name '*.h' \) -print0 |
xargs -0 -r -n 1 sh -c '
  clang-format-19 --style=file --Werror --dry-run "$1"
  result=$?
  if [ "$result" -ne 0 ]; then
    printf "\n[FORMAT ERROR] %s\n" "$1"
    exit "$result"
  fi
' sh
```

### Code static checker
```shell
set(CMAKE_CXX_CLANG_TIDY clang-tidy --config-file ${CMAKE_CURRENT_LIST_DIR}/.clang-tidy "--header-filter=${CMAKE_CURRENT_LIST_DIR}/(src|tests)/.*")
```

### Code dynamic checker
```shell
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")
```
