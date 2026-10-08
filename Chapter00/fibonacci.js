const fs = require('fs');

// 1. 读取编译好的 wasm 文件
const wasmBuffer = fs.readFileSync('fibonacci.wasm');

// 2. 提供 wasm 导入对象（fibonacci.wat 中声明了 import "console" "log"）
const importObject = {
  console: {
    log: (value) => console.log('Wasm Log:', value)
  }
};

// 3. 实例化并调用导出函数
WebAssembly.instantiate(wasmBuffer, importObject)
  .then(({ instance }) => {
    const { fibo } = instance.exports;

    console.log('fibo(5) =', fibo(5));
  })
  .catch(console.error);
