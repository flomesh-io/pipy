#!/usr/bin/env node

import fs from 'fs';
import got from 'got';
import chalk from 'chalk';

import { spawn } from 'child_process';
import { join, dirname, basename } from 'path';
import { program } from 'commander';

const log = console.log;
const error = (...args) => log.apply(this, [chalk.bgRed('ERROR')].concat(args.map(a => chalk.red(a))));
const sleep = (t) => new Promise(resolve => setTimeout(resolve, t * 1000));
const currentDir = dirname(new URL(import.meta.url).pathname);
const pipyBinPath = join(currentDir, '../../bin/pipy');
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
  const proc = startProcess(
    pipyBinPath, ['--no-graph', url, ...(opt?.options || [])],
    line => {
      if (!opt?.silent || !started) {
        log(chalk.bgGreen('worker >>>'), line);
        if (line.indexOf('Thread 0 started') >= 0) {
          started = true;
        }
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

//
// Randomly reload Pipy worker
//

function startReloading() {
}

//
// Run a test
//

async function runTest(name, options) {
  const basePath = join(currentDir, name);
  const workers = ['server', 'proxy', 'client'].map(name => ({ name }));

  try {
    log('Uploading codebases', chalk.magenta(name), '...');
    for (const w of workers) {
      await uploadCodebase(`test/${name}/${w.name}`, join(basePath, `${w.name}.js`));
    }

    log('Starting codebases', chalk.magenta(name), '...');
    for (const w of workers) {
      log(`Starting ${w.name}...`);
      w.worker = await startCodebase(
        `http://localhost:6060/repo/test/${name}/${w.name}/`, {
          silent: true,
          options: w.name === 'proxy' ? ['--admin-port=7070', '--threads=max', '--reuse-port'] : (w.name === 'client' ? ['--admin-port=7071'] : []),
        }
      );
      w.worker.on('exit', code => w.exitCode = code);
    }

    log('Codebases', chalk.magenta(name), 'all started');

    const reloadTimer = setInterval(() => {
      if (options.reload) {
        got.patch({
          url: `http://localhost:6060/api/v1/repo/test/${name}/proxy`,
          json: { version: (Date.now() / 1000) | 0 },
        }).catch(err => error(err));
      }
    }, 10000);

    const errors = await dump(options.duration || 60);

    clearInterval(reloadTimer);

    log('Stopping all workers...');

    workers.forEach(w => w.worker.kill('SIGINT'));
    for (let i = 0; i < 90 && workers.some(w => w.exitCode === undefined); i++) await sleep(1);
    if (workers.some(w => w.exitCode === undefined)) throw new Error('Worker did not quit timely');
    log('Workers exited');

    testResults[name] = (errors === 0);

  } catch (e) {
    testResults[name] = false;
    workers.forEach(w => w.worker?.kill?.());
    throw e;
  }
}

//
// Dump stats
//

async function dump(count) {
  const KB = 1024;
  const MB = 1024*KB;
  const GB = 1024*MB;

  function prettySize(size) {
    if (size >= GB) {
      return (size / GB).toFixed(2) + 'GB';
    } else if (size >= MB) {
      return (size / MB).toFixed(2) + 'MB';
    } else if (size >= KB) {
      return (size / KB).toFixed(2) + 'KB';
    } else {
      return size + 'B';
    }
  }

  let currentRequests = 0;
  let currentErrors = 0;
  let unchangeCount = 0;

  for (let i = 0; i < count; i++) {
    try {
      const metrics = await got.get('http://localhost:6060/metrics');
      const dump = await got.get('http://localhost:7070/dump');
      const requests = metrics.body.split('\n').find(l => l.startsWith('requests'))?.split?.(' ')?.[1] | 0;
      const errors = metrics.body.split('\n').find(l => l.startsWith('errors'))?.split?.(' ')?.[1] | 0;
      const stats = JSON.parse(dump.body);
      const pools = Object.values(stats.pools).map(i => i.size).reduce((a, b) => a + b);
      const inbound = stats.inbound.map(i => i.connections).reduce((a, b) => a + b);
      const outbound = stats.outbound.map(i => i.connections).reduce((a, b) => a + b);

      let requestRate = requests.toString();
      if (i > 0) {
        if (requests === currentRequests) {
          unchangeCount++;
        } else {
          requestRate += ' (';
          requestRate += ((requests - currentRequests) / (unchangeCount + 1)).toFixed(2);
          requestRate += ' per sec)';
          unchangeCount = 0;
        }
      }

      currentRequests = requests;
      currentErrors = errors;

      log(
        chalk.magenta((count - i) + 's'), ' ',
        'Pool size:', chalk.green(prettySize(pools)),
        'Inbound connections:', chalk.yellow(inbound),
        'Outbound connections:', chalk.yellow(outbound),
        'Errors:', chalk.yellow(errors),
        'Requests:', chalk.yellow(requestRate),
      );
    } catch (e) {
      error('Unable to dump stats');
    }

    await sleep(1);
  }

  return currentErrors;
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
    log('Starting repo...');
    repo = await startRepo();

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
  .option('-d, --duration <seconds>', 'Test duration in seconds')
  .option('--no-reload', 'Without reloading tests')
  .action((id, options) => start(id, options))
  .parse(process.argv)
