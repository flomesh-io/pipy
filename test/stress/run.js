#!/usr/bin/env node

import fs from 'fs';
import got from 'got';
import chalk from 'chalk';

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

async function startTest(name) {
  const basePath = join(currentDir, name);

  log('Uploading codebase', chalk.magenta(name), '...');
  await uploadCodebase(`test/${name}`, basePath);

  log('Starting codebase...');
  const worker = await startCodebase(`http://localhost:6060/repo/test/${name}/`);

  log('Codebase', chalk.magenta(name), 'started');
  return worker;
}

async function startTestcase(id) {
  id = id.toString();
  id = '0'.repeat(Math.max(0, 3 - id.length)) + id;
  const name = `test-${id}`;

  let worker;
  try {
    worker = await startTest(name);

    const f = await import(join(currentDir, name, 'test.js'));
    f.default({});

    worker.kill('SIGINT');

    for (let i = 0; i < 10 && worker.exitCode === null; i++) await sleep(1);
    if (worker.exitCode === null) throw new Error('Worker did not quit timely');
    log('Worker exited with code', worker.exitCode);

  } catch (e) {
    if (worker) worker.kill();
    throw e;
  }
}

async function start(id) {
  let repo;
  try {
    log('Starting repo...');
    repo = await startRepo();
    await startTestcase(id);
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
  .argument('<testcase-id>')
  .option('-p, --pipy', 'Run the Pipy codebase under testing')
  .option('-c, --client', 'Run the test client')
  .option('-s, --server', 'Run the mock server')
  .action((id, options) => start(id, options))
  .parse(process.argv)
