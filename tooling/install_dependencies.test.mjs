import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { mkdtemp, mkdir, readFile, rm, symlink, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { test } from 'node:test';

const script = fileURLToPath(new URL('./install_dependencies.sh', import.meta.url));

async function fixture(t) {
  const folder = await mkdtemp(path.join(tmpdir(), 'toolbox-install-test-'));
  const bin = path.join(folder, 'bin');
  await mkdir(bin);
  t.after(() => rm(folder, { recursive: true, force: true }));
  const log = path.join(folder, 'apt.log');
  const env = { ...process.env, PATH: bin, INSTALL_TEST_LOG: log, INSTALL_TEST_BIN: bin };
  async function executable(name, body) {
    await writeFile(path.join(bin, name), `#!/bin/bash\nset -eu\n${body}\n`, { mode: 0o755 });
  }
  async function tools({ oldNode = false } = {}) {
    await executable('node', oldNode
      ? 'if [[ "$1" == "--version" ]]; then echo v18.16.0; else exit 1; fi'
      : 'if [[ "$1" == "--version" ]]; then echo v22.0.0; fi');
    for (const name of ['wast2json', 'wasm-objdump', 'llvm-mc', 'aarch64-linux-gnu-objdump']) {
      await executable(name, `echo '${name} test version'`);
    }
  }
  function run(args = []) {
    return spawnSync('/bin/bash', [script, ...args], { env, encoding: 'utf8' });
  }
  return { bin, log, env, executable, tools, run };
}

test('dependency checking never invokes apt or sudo', async t => {
  const f = await fixture(t);
  await f.tools();
  await f.executable('apt-get', 'echo unexpected apt >&2; exit 99');
  await f.executable('sudo', 'echo unexpected sudo >&2; exit 99');
  const result = f.run(['--check']);
  assert.equal(result.status, 0, result.stderr);
  assert.match(result.stdout, /aarch64-linux-gnu-objdump/);
  assert.doesNotMatch(result.stderr, /unexpected/);
  const install = f.run();
  assert.equal(install.status, 0, install.stderr);
  assert.match(install.stdout, /Dependencies are ready/);
});

test('missing and outdated dependencies fail check mode without installation', async t => {
  const f = await fixture(t);
  const missing = f.run(['--check']);
  assert.equal(missing.status, 1);
  assert.match(missing.stderr, /\[MISSING\] node/);
  await f.tools({ oldNode: true });
  const old = f.run(['--check']);
  assert.equal(old.status, 1);
  assert.match(old.stderr, /Node.js must be >= 18.17/);
});

test('installation uses only the required apt package and verifies afterwards', async t => {
  const f = await fixture(t);
  await f.tools();
  await rm(path.join(f.bin, 'wast2json'));
  await f.executable('apt-get', `
    printf '%s\\n' "$*" >> "$INSTALL_TEST_LOG"
    if [[ "$1" == install ]]; then
      printf '#!/bin/bash\\necho installed-wast2json\\n' > "$INSTALL_TEST_BIN/wast2json"
      /bin/chmod +x "$INSTALL_TEST_BIN/wast2json"
    fi`);
  await f.executable('sudo', '[[ "$1" == "-A" ]]; shift; exec "$@"');
  const result = f.run();
  assert.equal(result.status, 0, result.stderr);
  assert.equal(await readFile(f.log, 'utf8'), 'update\ninstall -y --no-install-recommends wabt\n');
  assert.match(result.stdout, /installed-wast2json/);
});

test('apt failures and insufficient repository versions do not report success', async t => {
  const f = await fixture(t);
  await f.tools({ oldNode: true });
  await f.executable('sudo', '[[ "$1" == "-A" ]]; shift; exec "$@"');
  await f.executable('apt-get', 'echo simulated-network-error >&2; exit 23');
  const failed = f.run();
  assert.equal(failed.status, 23);
  assert.match(failed.stderr, /simulated-network-error/);
  assert.doesNotMatch(failed.stdout, /Dependencies are ready/);
  await f.executable('apt-get', 'exit 0');
  const old = f.run();
  assert.equal(old.status, 1);
  assert.match(old.stderr, /Dependency verification failed/);
  assert.doesNotMatch(old.stdout, /Dependencies are ready/);
});

test('unsupported package managers and invalid options are explicit errors', async t => {
  const f = await fixture(t);
  const unsupported = f.run();
  assert.equal(unsupported.status, 1);
  assert.match(unsupported.stderr, /requires apt-get/);
  assert.equal(f.run(['--unknown']).status, 1);
  assert.equal(f.run(['--check', 'extra']).status, 1);
  assert.equal(f.run(['--help']).status, 0);
});

test('version checking uses the actual Node.js version', async t => {
  const f = await fixture(t);
  await f.tools();
  await rm(path.join(f.bin, 'node'));
  await symlink(process.execPath, path.join(f.bin, 'node'));
  const result = f.run(['--check']);
  assert.equal(result.status, 0, result.stderr);
});
