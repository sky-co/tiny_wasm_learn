import { spawn } from 'node:child_process';
import { createServer } from 'node:http';
import { mkdtemp, readFile, rm, stat, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const directory = path.dirname(fileURLToPath(import.meta.url));
const projectRoot = path.dirname(directory);
const maxFileSize = 16 * 1024 * 1024;
const extensions = new Set(['.wasm', '.wast', '.wat']);

class RequestError extends Error {
  constructor(message, status = 400) {
    super(message);
    this.status = status;
  }
}

function text(value, name) {
  if (typeof value !== 'string' || !value.trim()) {
    throw new RequestError(`${name} must not be empty.`);
  }
  return value.trim();
}

function command(program, args, input) {
  return new Promise((resolve, reject) => {
    const child = spawn(program, args, { stdio: ['pipe', 'pipe', 'pipe'] });
    const stdout = [];
    const stderr = [];
    let size = 0;
    let failure;
    const timer = setTimeout(() => {
      failure = new Error(`${program} timed out after 30 seconds.`);
      child.kill();
    }, 30_000);
    function collect(target, chunk) {
      size += chunk.length;
      if (size > 4 * 1024 * 1024) {
        failure = new Error(`${program} output exceeds the 4 MiB limit.`);
        child.kill();
      } else {
        target.push(chunk);
      }
    }
    child.stdout.on('data', chunk => collect(stdout, chunk));
    child.stderr.on('data', chunk => collect(stderr, chunk));
    child.stdin.on('error', error => {
      if (error.code !== 'EPIPE') failure = error;
    });
    child.on('error', error => {
      clearTimeout(timer);
      reject(new Error(error.code === 'ENOENT'
        ? `${program} was not found. Install the dependency and check PATH.` : error.message));
    });
    child.on('close', code => {
      clearTimeout(timer);
      const out = Buffer.concat(stdout).toString();
      const err = Buffer.concat(stderr).toString();
      if (failure) reject(failure);
      else if (code !== 0) reject(new RequestError(`${program} failed (${code}):\n${err || out}`));
      else resolve({ stdout: out, stderr: err });
    });
    child.stdin.end(input);
  });
}

async function readJson(request) {
  const chunks = [];
  let length = 0;
  for await (const chunk of request) {
    length += chunk.length;
    if (length > 64 * 1024) throw new RequestError('Request exceeds the 64 KiB limit.', 413);
    chunks.push(chunk);
  }
  try {
    const value = JSON.parse(Buffer.concat(chunks).toString());
    if (!value || typeof value !== 'object' || Array.isArray(value)) {
      throw new RequestError('Request must be a JSON object.');
    }
    return value;
  } catch (error) {
    if (error instanceof SyntaxError) throw new RequestError('Invalid JSON request.');
    throw error;
  }
}

export async function createToolServer() {
  const cacheDirectory = await mkdtemp(path.join(tmpdir(), 'wasm-interpreter-'));

  async function resolveSource(source) {
    source = text(source, 'File path or URL');
    if (/^https?:\/\//i.test(source)) {
      const url = new URL(source);
      const name = path.basename(decodeURIComponent(url.pathname));
      if (!extensions.has(path.extname(name))) {
        throw new RequestError('URL must point to a .wasm, .wast, or .wat file.');
      }
      const response = await fetch(url, { signal: AbortSignal.timeout(15_000) });
      if (!response.ok) throw new RequestError(`Download failed: HTTP ${response.status}.`);
      const chunks = [];
      let size = 0;
      for await (const chunk of response.body) {
        size += chunk.length;
        if (size > maxFileSize) {
          throw new RequestError('Remote file exceeds the 16 MiB limit.', 413);
        }
        chunks.push(chunk);
      }
      const folder = await mkdtemp(path.join(cacheDirectory, 'download-'));
      const filename = path.join(folder, name);
      await writeFile(filename, Buffer.concat(chunks));
      return filename;
    }
    const filename = path.resolve(projectRoot, source);
    if (!extensions.has(path.extname(filename))) {
      throw new RequestError('Select a .wasm, .wast, or .wat file.');
    }
    const info = await stat(filename);
    if (!info.isFile()) throw new RequestError('Path is not a regular file.');
    if (info.size > maxFileSize) throw new RequestError('Local file exceeds the 16 MiB limit.', 413);
    return filename;
  }

  async function action(name, data) {
    if (name === 'assemble') {
      const instruction = text(data.instruction, 'Assembly instruction');
      if (!/^[a-z][a-z0-9.]*(?:[ \t]+[^\r\n;]+)?$/i.test(instruction)) {
        throw new RequestError('Enter a single AArch64 instruction. Multiple lines, instruction separators, and assembler directives are not supported.');
      }
      const result = await command('llvm-mc', ['-triple=aarch64', '-show-encoding'], `${instruction}\n`);
      const encodings = [...result.stdout.matchAll(/encoding:\s*\[([^\]]+)\]/g)];
      const values = encodings[0]?.[1].split(',').map(value => value.trim());
      if (encodings.length !== 1 || values?.length !== 4
          || !values.every(value => /^0x[0-9a-f]{2}$/i.test(value))) {
        throw new RequestError(`Could not encode a complete 4-byte instruction (unresolved symbols are not supported):\n${result.stdout}${result.stderr}`);
      }
      const bytes = Buffer.from(values.map(value => Number.parseInt(value, 16)));
      return {
        bytes: [...bytes].map(value => value.toString(16).padStart(2, '0').toUpperCase()).join(' '),
        word: `0x${bytes.readUInt32LE().toString(16).padStart(8, '0').toUpperCase()}U`,
        output: result.stdout + result.stderr,
      };
    }
    if (name === 'disassemble') {
      const match = /^(?:0x)?([0-9a-f]{8})u?$/i.exec(text(data.code, 'Machine code'));
      if (!match) throw new RequestError('Enter an 8-digit hexadecimal instruction word, such as 0xD10043FFU.');
      const bytes = Buffer.alloc(4);
      bytes.writeUInt32LE(Number.parseInt(match[1], 16));
      const folder = await mkdtemp(path.join(cacheDirectory, 'instruction-'));
      try {
        const filename = path.join(folder, 'ret.bin');
        await writeFile(filename, bytes);
        const result = await command('aarch64-linux-gnu-objdump',
          ['-D', '-b', 'binary', '-m', 'aarch64', '-EL', filename]);
        return {
          bytes: [...bytes].map(value => value.toString(16).padStart(2, '0').toUpperCase()).join(' '),
          output: result.stdout + result.stderr,
        };
      } finally {
        await rm(folder, { recursive: true, force: true });
      }
    }
    if (!['convert', 'objdump'].includes(name)) {
      throw new RequestError('API endpoint not found.', 404);
    }
    const filename = await resolveSource(data.source);
    if (name === 'convert') {
      if (path.extname(filename) === '.wasm') {
        return { modules: [filename], jsonPath: null, manifest: null,
          output: 'Input is already a Wasm binary. No wast2json conversion is needed; it can be disassembled directly.' };
      }
      const jsonPath = filename.replace(/\.(wast|wat)$/, '.json');
      const result = await command('wast2json', [filename, '-o', jsonPath]);
      const manifest = JSON.parse(await readFile(jsonPath, 'utf8'));
      const modules = manifest.commands.filter(entry => entry.type === 'module')
        .map(entry => path.resolve(path.dirname(jsonPath), entry.filename));
      return { jsonPath, manifest, modules, output: result.stdout + result.stderr };
    }
    if (path.extname(filename) !== '.wasm') {
      throw new RequestError('Run wast2json first, then select a generated .wasm module.');
    }
    const result = await command('wasm-objdump', ['-d', '-x', filename]);
    return { output: result.stdout + result.stderr };
  }

  const server = createServer(async (request, response) => {
    function send(status, value, type = 'application/json; charset=utf-8') {
      response.writeHead(status, {
        'Content-Type': type,
        'X-Content-Type-Options': 'nosniff',
        'Cache-Control': 'no-store',
        'Content-Security-Policy': "frame-ancestors 'none'",
      });
      response.end(type.startsWith('application/json') ? JSON.stringify(value) : value);
    }
    try {
      const port = server.address().port;
      const hosts = [`127.0.0.1:${port}`, `localhost:${port}`];
      if (!hosts.includes(request.headers.host)) throw new RequestError('Host is not allowed.', 403);
      const origin = `http://${request.headers.host}`;
      if ((request.headers.origin && request.headers.origin !== origin)
          || request.headers['sec-fetch-site'] === 'cross-site') {
        throw new RequestError('Only same-origin requests are allowed.', 403);
      }
      if (request.method === 'GET'
          && ['/', '/wasm_interpreter.html'].includes(request.url)) {
        send(200, await readFile(path.join(directory, 'wasm_interpreter.html')), 'text/html; charset=utf-8');
      } else if (request.method === 'POST' && request.url.startsWith('/api/')) {
        if (request.headers['content-type']?.split(';')[0] !== 'application/json') {
          throw new RequestError('Use application/json.', 415);
        }
        send(200, await action(request.url.slice(5), await readJson(request)));
      } else {
        throw new RequestError('Page or API endpoint not found.', 404);
      }
    } catch (error) {
      send(error.status || 500, { error: error.message });
    }
  });
  return {
    server,
    async close() {
      if (server.listening) {
        await new Promise((resolve, reject) => {
          server.close(error => error ? reject(error) : resolve());
          server.closeIdleConnections();
        });
      }
      await rm(cacheDirectory, { recursive: true, force: true });
    },
  };
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const port = Number(process.env.PORT || 1818);
  if (!Number.isInteger(port) || port < 1 || port > 65535) {
    throw new Error('PORT must be an integer between 1 and 65535.');
  }
  const app = await createToolServer();
  app.server.on('error', error => {
    console.error(`Failed to start the service: ${error.message}`);
    process.exitCode = 1;
    app.close().catch(error => console.error(`Cleanup failed: ${error.message}`));
  });
  app.server.listen(port, '127.0.0.1', () => {
    console.log(`Open http://127.0.0.1:${port}/wasm_interpreter.html`);
  });
  for (const signal of ['SIGINT', 'SIGTERM']) {
    process.once(signal, () => {
      app.close().catch(error => {
        console.error(`Cleanup failed: ${error.message}`);
        process.exitCode = 1;
      });
    });
  }
}
