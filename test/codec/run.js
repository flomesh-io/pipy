#!/usr/bin/env node

import fs from 'fs';
import chalk from 'chalk';

import { spawn } from 'child_process';
import { join, dirname } from 'path';
import { program } from 'commander';

const log = console.log;
const error = (...args) => log.apply(this, [chalk.bgRed('ERROR')].concat(args.map(a => chalk.red(a))));
const sleep = (t) => new Promise(resolve => setTimeout(resolve, t * 1000));
const currentDir = dirname(new URL(import.meta.url).pathname);
const pipyBinPath = join(currentDir, '../../bin/pipy');

function startProcess(cmd, args, onStderr, onStdout) {
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
        onStderr(line);
      }
      i = j + 1;
    }
  });
  proc.stdout.on('data', onStdout);
  return proc;
}

function startPipy(filename, onStdout) {
  return startProcess(
    pipyBinPath, ['--no-graph', filename],
    line => log(chalk.bgGreen('worker >>>'), line),
    onStdout
  );
}

async function runTest(name) {
  const basePath = join(currentDir, name);

  let worker;
  try {
    log(`Testing ${chalk.cyan(name)}...`);
    const stdoutBuffer = [];
    worker = startPipy(
      `${basePath}/main.js`,
      data => stdoutBuffer.push(data)
    );

    let exitCode;
    worker.on('exit', code => exitCode = code);

    for (let i = 0; i < 10 && exitCode === undefined; i++) await sleep(1);
    if (exitCode === undefined) throw new Error('Worker did not quit timely');
    log('Worker exited with code', exitCode);

    const stdout = Buffer.concat(stdoutBuffer);
    const expected = fs.readFileSync(`${basePath}/output`);
    if (Buffer.compare(stdout, expected)) {
      error(`Test ${name} did not output expected data`);
    } else {
      log(`Test ${chalk.cyan(name)} OK`);
    }

  } catch (e) {
    if (worker) worker.kill();
    throw e;
  }
}

async function start(id) {
  try {
    if (id) {
      await runTest(id, options);

    } else {
      const entries = fs.readdirSync(currentDir, { withFileTypes: true }).filter(e => e.isDirectory());
      for (const ent of entries) {
        await runTest(ent.name);
      }
    }

  } catch (e) {
    error(e.message);
    log(e);
    process.exit(-1);
  }

  log('All tests done.');
  process.exit(0);
}

program
  .argument('[testcase-id]')
  .action(id => start(id))
  .parse(process.argv)
