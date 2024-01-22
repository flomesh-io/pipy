#!/usr/bin/env node

import os from 'os';
import fs from 'fs';
import url from 'url';
import net from 'net';
import got from 'got';
import chalk from 'chalk';
import logUpdate from 'log-update';

import { spawn } from 'child_process';
import { join, dirname, basename } from 'path';
import { program } from 'commander';

const log = console.log;
const error = (...args) => log.apply(this, [chalk.bgRed('ERROR')].concat(args.map(a => chalk.red(a))));
const sleep = (t) => new Promise(resolve => setTimeout(resolve, t * 1000));
const currentDir = dirname(url.fileURLToPath(import.meta.url));
const pipyBinName = os.platform() === 'win32' ? '..\\..\\bin\\Release\\pipy.exe' : '../../bin/pipy';
const pipyBinPath = join(currentDir, pipyBinName);
const allTests = [];
const testResults = {};

//
// Find all testcases
//

fs.readdirSync(currentDir, { withFileTypes: true })
  .filter(ent => ent.isDirectory())
  .forEach(ent => {
    const n = parseInt(ent.name.substring(0, 3));
    if (!isNaN(n)) allTests[n] = ent.name;
  });

//
// Spawn a process
//

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

//
// Start Pipy repo
//

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

//
// Upload a codebase to Pipy repo
//

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
    if (fs.statSync(basePath).isDirectory()) {
      await uploadDir('/');
    } else {
      const name = basename(basePath);
      log('Uploading', name);
      const body = fs.readFileSync(basePath);
      await client.post(
        join(codebaseFilePath, 'main.js'),
        { body }
      )
    }
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

//
// Start a Pipy worker
//

async function startCodebase(url, opt) {
  let started = false;
  return await Promise.race([
    new Promise(
      resolve => {
        const proc = startProcess(
          pipyBinPath, ['--no-graph', url, ...(opt?.options || [])],
          line => {
            if (!opt?.silent || !started) {
              log(chalk.bgGreen('worker >>>'), line);
              if (line.indexOf('Thread 0 started') >= 0) {
                log('Worker started');
                started = true;
                resolve(proc);
              }
            }
          }
        );
      }
    ),
    sleep(10).then(() => Promise.reject(new Error('Failed starting worker'))),
  ]);
}

//
// Start testing sessions
//

function createSessions(proc, port, options) {

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

  class Session {
    constructor(id, messages, verify, delay) {
      this.id = id;
      this.messages = messages.map(
        msg => msg.map(
          evt => {
            if (Buffer.isBuffer(evt)) {
              return evt;
            } else if (typeof evt === 'string') {
              return Buffer.from(evt);
            } else {
              throw new Error('Events are expected to be Buffers or strings');
            }
          }
        )
      );
      this.cursorSend = 0;
      this.cursorSendEvent = 0;
      this.cursorVerify = 0;
      this.verify = verify;
      this.delay = delay || 0;
      this.socket = null;
      this.responseBuffer = [];
      this.sentSize = 0;
      this.receivedSize = 0;
      this.error = null;
      this.done = false;
    }

    step() {
      if (this.error) {
        throw this.error;
      } else if (this.done) {
        return false;
      } if (this.delay > 0) {
        this.delay--;
        return true;
      } else if (this.cursorSend < this.messages.length) {
        if (this.socket) {
          return new Promise(resolve => {
            const msg = this.messages[this.cursorSend];
            const evt = msg[this.cursorSendEvent++];
            if (this.cursorSendEvent >= msg.length) {
              this.cursorSendEvent = 0;
              this.cursorSend++;
            }
            this.socket.write(
              evt,
              () => {
                this.sentSize += evt.byteLength;
                resolve(true);
              }
            );
          });
        } else {
          return new Promise((resolve, reject) => {
            const s = net.createConnection({ host: '127.0.0.1', port }, () => resolve(true));
            s.on('data', data => {
              this.responseBuffer.push(data);
              this.receivedSize += data.byteLength;
            });
            s.on('end', () => {
              this.socket = null;
              try {
                this.check();
              } catch (e) {
                this.error = e;
              }
            });
            s.on('error', err => reject(err));
            this.socket = s;
            this.cursorSend = this.cursorVerify;
            this.cursorSendEvent = 0;
          });
        }
      } else {
        if (this.socket) {
          this.socket.end();
          this.socket = null;
        }
        return true;
      }
    }

    check() {
      let p = 0;
      const data = Buffer.concat(this.responseBuffer);
      this.responseBuffer.length = 0;

      function readLine() {
        const start = p;
        while (p < data.byteLength) {
          if (data[p] === 13 && data[p+1] === 10) break;
          p++;
        }
        const str = data.subarray(start, p).toString();
        if (p < data.byteLength) p += 2;
        return str;
      }

      function readBlock(size) {
        const block = data.subarray(p, p + size);
        p += size;
        return block.toString();
      }

      while (p < data.byteLength) {
        const head = readLine();
        const status = parseInt(head.split(' ')[1]);
        if (!(100 <= status && status <= 599)) {
          throw new Error(`Invalid status code ${status} in response from session #${this.id}`);
        }

        const headers = {};
        for (;;) {
          const line = readLine();
          if (line.length === 0) break;
          const segs = line.split(':');
          const k = segs[0] || '';
          const v = segs[1] || '';
          headers[k.toLowerCase()] = v.trim();
        }
        let body = '';
        if (headers['transfer-encoding'] === 'chunked') {
          for (;;) {
            const line = readLine();
            const size = parseInt(line, 16);
            if (size >= 0) {
              body += readBlock(size);
            } else {
              throw new Error(`Invalid chunked encoding in response from session #${this.id}`);
            }
            if (readLine() !== '') throw new Error(`Invalid chunked encoding in response from session #${this.id}`);
            if (size === 0) break;
          }
        } else if ('content-length' in headers) {
          const length = parseInt(headers['content-length']);
          if (length >= 0) {
            body = readBlock(length);
          } else {
            throw new Error(`Invalid Content-Length in response from session #${this.id}`);
          }
        }

        if (this.cursorVerify >= this.cursorSend) {
          throw new Error(`Received extra responses from session #${this.id}`);
        }
        this.verify({ status, headers, body }, this.cursorVerify);
        if (++this.cursorVerify >= this.messages.length) {
          this.done = true;
        }
      }
    }

    stats() {
      let status;
      if (this.done) {
        status = 'DONE';
      } else if (this.delay > 0) {
        status = 'IDLE';
      } else if (this.cursorSend < this.messages.length) {
        status = 'SEND';
      } else {
        status = 'WAIT';
      }
      return (
        formatPadding(12, `Session #${this.id}`) +
        formatPadding( 6, status) +
        formatPadding(20, 'UP ' + formatSize(this.sentSize)) +
        formatPadding( 0, 'DOWN ' + formatSize(this.receivedSize))
      );
    }
  }

  const sessions = [];
  const reloads = [];

  function dumpStats(t) {
    logUpdate(
      `T = ${t}\n` +
      sessions.map(a => a.stats()).join('\n')
    );
  }

  function triggerReload() {
    logUpdate.done();
    proc.kill('SIGHUP');
  }

  function session({ delay, messages, verify }) {
    const a = new Session(sessions.length, messages, verify, delay || 0);
    sessions.push(a);
    return a;
  }

  function reload(t) {
    if (options.reload) {
      reloads[t] = true;
    }
  }

  async function run() {
    let tick = 0;

    for (;;) {
      let done = true;
      for (let i = 0, n = sessions.length; i < n; i++) {
        try {
          if (await sessions[i].step()) {
            done = false;
          }
        } catch (e) {
          error(`Error from session #${i}`);
          throw e;
        }
      }
      if (options.stats) {
        dumpStats(tick);
      } else {
        log(`T=${tick}`);
      }
      if (done) break;
      if (reloads[tick++]) triggerReload();
      await sleep(0.1);
    }

    logUpdate.done();
  }

  return { session, run, reload };
}

//
// Run a test
//

async function runTest(name, options) {
  const basePath = join(currentDir, name);

  let worker;
  try {
    if (options.pipy) {
      log('Uploading codebase', chalk.magenta(name), '...');
      await uploadCodebase(`test/${name}`, basePath);
      log('Starting codebase...');
      worker = await startCodebase(`http://localhost:6060/repo/test/${name}/`, { silent: true });
      log('Codebase', chalk.magenta(name), 'started');
    }

    function http(method, path, headers, body) {
      if (typeof headers !== 'object') {
        body = headers;
        headers = {};
      }
      body = Buffer.from(body || '');
      const hasBody = !(method === 'GET' || method === 'HEAD' || method === 'DELETE');
      const head = [
        `${method} ${path} HTTP/1.1`,
        ...Object.entries(headers).map(([k, v]) => `${k}: ${v}`),
        hasBody ? `Content-Length: ${body.byteLength}\r\n` : '',
      ].join('\r\n');
      return [Buffer.concat([Buffer.from(head + '\r\n'), body])];
    }

    function split(count, buffers) {
      const buffer = Buffer.concat(buffers);
      const size = Math.ceil(buffer.byteLength / count);
      return new Array(count).fill(0).map((_, i) => buffer.subarray(i * size, Math.min(i * size + size, buffer.byteLength)));
    }

    function repeat(count, messages) {
      return new Array(count).fill(messages).flat()
    }

    const { session, reload, run } = createSessions(worker, 8000, options);
    const f = await import(url.pathToFileURL(join(currentDir, name, 'test.js')));
    f.default({ session, http, split, repeat, reload });

    log('Running...');
    await run();
    log('All sessions done');

    if (options.pipy) {
      worker.kill('SIGINT');
      await Promise.race([
        new Promise(
          resolve => {
            worker.on('exit', code => {
              log('Worker exited with code', code);
              resolve();
            })
          }
        ),
        sleep(10).then(() => { throw new Error('Worker did not quit timely'); }),
      ]);
    }

    testResults[name] = true;

  } catch (e) {
    testResults[name] = false;
    if (worker) worker.kill();
    throw e;
  }
}

//
// Summarize test results
//

function summary() {
  const maxWidth = Math.max.apply(null, Object.keys(testResults).map(name => name.length));
  const width = maxWidth + 20;
  log('='.repeat(width));
  log('Summary');
  log('-'.repeat(width));
  Object.keys(testResults).sort().forEach(
    name => {
      if (testResults[name]) {
        log(name + ' '.repeat(width - 2 - name.length) + chalk.green('OK'));
      } else {
        log(name + ' '.repeat(width - 4 - name.length) + chalk.red('FAIL'));
      }
    }
  );
  log('='.repeat(width));
}

//
// Starting point
//

async function start(id, options) {
  let repo;
  try {
    if (options.pipy) {
      log('Starting repo...');
      repo = await startRepo();
    }

    if (id === undefined) {
      for (const i in allTests) {
        await runTest(allTests[i], options);
      }
      summary();

    } else if (id in allTests) {
      await runTest(allTests[id], options);
      summary();

    } else {
      throw new Error('Unknown test ID');
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
  .option('--no-pipy', 'Without running Pipy')
  .option('--no-stats', 'Without showing stats')
  .option('--no-reload', 'Without reloading tests')
  .action((id, options) => start(id, options))
  .parse(process.argv)
