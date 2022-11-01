#!/usr/bin/env node

import fs from 'fs';
import net from 'net';
import got from 'got';
import chalk from 'chalk';
import logUpdate from 'log-update';

import { spawn } from 'child_process';
import { join, dirname } from 'path';
import { program } from 'commander';

const log = console.log;
const error = (...args) => log.apply(this, [chalk.bgRed('ERROR')].concat(args.map(a => chalk.red(a))));
const sleep = (t) => new Promise(resolve => setTimeout(resolve, t * 1000));
const currentDir = dirname(new URL(import.meta.url).pathname);
const pipyBinPath = join(currentDir, '../../bin/pipy');

function startProcess(cmd, args, onStdout) {
  const proc = spawn(cmd, args);
  const lineBuffer = [];
  proc.stderr.on('data', data => {
    let i = 0, n = data.length;
    while (i < n) {
      let j = i;
      while (j < n && data[j] !== 10) j++;
      if (j > i) lineBuffer.push(data.slice(i, j));
      if (j < n) {
        const line = Buffer.concat(lineBuffer).toString();
        lineBuffer.length = 0;
        onStdout(line);
      }
      i = j + 1;
    }
  });
  return proc;
}

async function startRepo() {
  let started = false;
  const proc = startProcess(
    pipyBinPath, [],
    line => {
      log(chalk.bgGreen('repo >>>'), line);
      if (line.indexOf('Listening on TCP port') >= 0) {
        started = true;
      }
    }
  );

  for (let i = 0; i < 10 && !started; i++) await sleep(1);
  if (started) {
    log('Repo started');
  } else {
    throw new Error('Failed starting repo');
  }

  return proc;
}

async function uploadCodebase(codebaseName, basePath) {
  log(`Creating codebase ${codebaseName}...`);

  const codebasePath = join('api/v1/repo', codebaseName);
  const codebaseFilePath = join('api/v1/repo-files', codebaseName);

  const client = got.extend({
    prefixUrl: 'http://localhost:6060',
  });

  try {
    await client.post(codebasePath, {
      json: {}
    });
  } catch (e) {
    throw new Error('Failed creating codebase');
  }

  async function uploadDir(dirName) {
    const dirPath = join(basePath, dirName);
    for (const name of fs.readdirSync(dirPath)) {
      const path = join(dirPath, name);
      if (fs.statSync(path).isDirectory()) {
        await uploadDir(join(dirName, name));
      } else {
        log('Uploading', path);
        const body = fs.readFileSync(path);
        await client.post(
          join(codebaseFilePath, dirName, name),
          { body }
        );
      }
    }
  }

  try {
    await uploadDir('/');
  } catch (e) {
    throw new Error('Failed uploading codebase files');
  }

  log(`Publishing codebase ${codebaseName}...`);
  try {
    await client.patch(codebasePath, {
      json: { version: 2 }
    });
  } catch (e) {
    throw new Error('Failed publishing codebase');
  }
}

async function startCodebase(url) {
  let started = false;
  const proc = startProcess(
    pipyBinPath, [url],
    line => {
      log(chalk.bgGreen('worker >>>'), line);
      if (line.indexOf('Listening on TCP port') >= 0) {
        started = true;
      }
    }
  );

  for (let i = 0; i < 10 && !started; i++) await sleep(1);
  if (started) {
    log('Worker started');
  } else {
    throw new Error('Failed starting worker');
  }

  return proc;
}

function createAttacks(port) {

  function formatSize(size) {
    let n, unit;
    if (size < 1024) {
      n = size;
      unit = 'B';
    } else if (size < 1024 * 1024) {
      n = size / 1024;
      unit = 'KB';
    } else {
      n = size / (1024 * 1024);
      unit = 'MB';
    }
    return n.toFixed(2) + unit;
  }

  function formatPadding(width, str) {
    return str + ' '.repeat(Math.max(0, width - str.length));
  }

  class Attack {
    constructor(id, events, verify, delay) {
      this.id = id;
      this.events = events.map(
        evt => {
          if (Buffer.isBuffer(evt)) {
            return evt;
          } else if (typeof evt === 'string') {
            return Buffer.from(evt);
          } else {
            throw new Error('Events are expected to be Buffers or strings');
          }
        }
      );
      this.cursor = 0;
      this.verify = verify;
      this.delay = delay || 0;
      this.socket = null;
      this.buffers = [];
      this.sentSize = 0;
      this.receivedSize = 0;
      this.writeEnd = false;
      this.readEnd = false;
      this.checked = false;
    }

    write() {
      if (this.writeEnd) {
        return false;
      } else if (this.delay > 0) {
        this.delay--;
        return true;
      } else if (!this.socket) {
        return new Promise((resolve, reject) => {
          const s = net.createConnection({ port }, () => resolve(true));
          s.on('data', data => { this.buffers.push(data); this.receivedSize += data.byteLength; });
          s.on('end', () => this.readEnd = true);
          s.on('error', err => reject(err));
          this.socket = s;
        });
      } else if (this.cursor < this.events.length) {
        return new Promise(resolve => {
          const evt = this.events[this.cursor++];
          this.socket.write(
            evt,
            () => {
              this.sentSize += evt.byteLength;
              resolve(true);
            }
          );
        });
      } else {
        this.socket.end();
        this.writeEnd = true;
        return false;
      }
    }

    check() {
      if (this.checked) return true;
      if (this.readEnd) {
        const f = this.verify;
        if (typeof f === 'function') {
          try {
            f(Buffer.concat(this.buffers));
          } catch (e) {
            error(`Verification error from attack #${this.id}`);
            throw e;
          }
        }
        this.checked = true;
        return true;
      }
      return false;
    }

    stats() {
      let status;
      if (this.checked) {
        status = 'DONE';
      } else if (this.writeEnd) {
        status = 'WAIT';
      } else if (this.delay > 0) {
        status = 'IDLE';
      } else {
        status = 'SEND';
      }
      return (
        formatPadding(12, `Attack #${this.id}`) +
        formatPadding( 6, status) +
        formatPadding(20, 'UP ' + formatSize(this.sentSize)) +
        formatPadding( 0, 'DOWN ' + formatSize(this.receivedSize))
      );
    }
  }

  const attacks = [];

  function dumpStats() {
    logUpdate(
      attacks.map(a => a.stats()).join('\n')
    );
  }

  function attack(start, events, verify) {
    const a = new Attack(attacks.length, events, verify, start);
    attacks.push(a);
    return a;
  }

  async function run() {
    for (;;) {
      let done = true;
      for (let i = 0, n = attacks.length; i < n; i++) {
        if (await attacks[i].write()) {
          done = false;
        }
      }
      dumpStats();
      if (done) break;
    }

    for (;;) {
      let checked = true;
      attacks.forEach(
        a => {
          if (!a.check()) checked = false;
        }
      );
      dumpStats();
      if (checked) break;
      await sleep(1);
    }

    logUpdate.done();
  }

  return { attack, run };
}

async function runTestByName(name) {
  const basePath = join(currentDir, name);

  let worker;
  try {
    log('Uploading codebase', chalk.magenta(name), '...');
    await uploadCodebase(`test/${name}`, basePath);
  
    log('Starting codebase...');
    worker = await startCodebase(`http://localhost:6060/repo/test/${name}/`);
  
    log('Codebase', chalk.magenta(name), 'started');

    const { attack, run } = createAttacks(8000);
    const f = await import(join(currentDir, name, 'test.js'));
    f.default({ attack });

    log('Running attacks...');
    await run();
    log('All attacks done');

    worker.kill('SIGINT');
    for (let i = 0; i < 10 && worker.exitCode === null; i++) await sleep(1);
    if (worker.exitCode === null) throw new Error('Worker did not quit timely');
    log('Worker exited with code', worker.exitCode);

  } catch (e) {
    if (worker) worker.kill();
    throw e;
  }
}

function runTest(id) {
  if (isNaN(parseInt(id))) {
    return runTestByName(id);
  } else {
    id = id.toString();
    id = '0'.repeat(Math.max(0, 3 - id.length)) + id;
    return runTestByName(`test-${id}`);
  }
}

async function start(id) {
  let repo;
  try {
    log('Starting repo...');
    repo = await startRepo();

    if (id) {
      await runTest(id);

    } else {
      const dirnames = fs.readdirSync(currentDir).filter(n => n.startsWith('test-'));
      for (const dir of dirnames) {
        await runTest(dir);
      }
    }

  } catch (e) {
    error(e.message);
    log(e);
    if (repo) repo.kill();
    process.exit(-1);
  }

  log('Test done.');
  if (repo) repo.kill();
  process.exit(0);
}

program
  .argument('[testcase-id]')
  .option('-p, --pipy', 'Run the Pipy codebase under testing')
  .option('-c, --client', 'Run the test client')
  .option('-s, --server', 'Run the mock server')
  .action((id, options) => start(id, options))
  .parse(process.argv)
