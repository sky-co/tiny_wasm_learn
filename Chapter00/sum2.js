const readline = require('readline');
const fs = require('fs');

// 兼容 Node.js 与浏览器环境的通用加载器
async function loadWasm(wasmPath = 'sum.wasm', customLog = console.log) {
  const importObject = {
    console: {
      log: (value) => customLog(`Wasm Log: ${value}`)
    }
  };

  if (typeof window === 'undefined') {
    const wasmBuffer = fs.readFileSync(wasmPath);
    const { instance } = await WebAssembly.instantiate(wasmBuffer, importObject);
    return instance.exports;
  }

  const { instance } = await WebAssembly.instantiateStreaming(fetch(wasmPath), importObject);
  return instance.exports;
}

// 主交互逻辑
async function main() {
  try {
    const wasmExports = await loadWasm('sum.wasm');

    // 1. 优先读取命令行参数：如 `node sum2.js 10`
    const arg = process.argv[2];
    if (arg !== undefined) {
      const num = parseInt(arg, 10);
      if (isNaN(num)) {
        console.error('错误: 请输入有效的整数！');
        process.exit(1);
      }
      const result = wasmExports.sum(num);
      console.log(`\n计算结果: 1 到 ${num} 的和为: ${result}`);
      return;
    }

    // 2. 无命令行参数时，通过终端交互式输入
    const rl = readline.createInterface({
      input: process.stdin,
      output: process.stdout
    });

    rl.question('请输入一个正整数 n: ', (answer) => {
      const num = parseInt(answer.trim(), 10);
      if (isNaN(num)) {
        console.error('错误: 输入不是有效的整数！');
      } else {
        const result = wasmExports.sum(num);
        console.log(`\n计算结果: 1 到 ${num} 的和为: ${result}`);
      }
      rl.close();
    });
  } catch (err) {
    console.error('运行出错:', err);
  }
}

if (typeof window === 'undefined') {
  main();
}
