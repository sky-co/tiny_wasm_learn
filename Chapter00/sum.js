const fs = require('fs');

// 1. 读取编译好的 wasm 文件
const wasmBuffer = fs.readFileSync('sum.wasm');

// 2. 提供 wasm 导入对象（sum.wat 中声明了 import "console" "log"）
const importObject = {
  console: {
    log: (value) => console.log('Wasm Log:', value)
  }
};

// 3. 实例化并调用导出函数
WebAssembly.instantiate(wasmBuffer, importObject)
  .then(({ instance }) => {
    const { sum } = instance.exports;

    console.log('sum(5) (1+2+...+5)  =', sum(5));
  })
  .catch(console.error);
