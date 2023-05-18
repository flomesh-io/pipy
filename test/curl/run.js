#!/usr/bin/env node

import fs from 'fs';
import chalk from 'chalk';

import { spawn, execFile } from 'child_process';
import { join, dirname } from 'path';
import { program } from 'commander';

const log = console.log;
const error = (...args) => log.apply(this, [chalk.bgRed('ERROR')].concat(args.map(a => chalk.red(a))));
const sleep = (t) => new Promise(resolve => setTimeout(resolve, t * 1000));

const currentDir = dirname(new URL(import.meta.url).pathname);
const pipyBinPath = join(currentDir, '../../bin/pipy');
const allTests = [];
const testResults = {};

fs.readdirSync(currentDir, { withFileTypes: true })
  .filter(ent => ent.isDirectory())
  .forEach(ent => {
    const n = parseInt(ent.name.substring(0, 3));
    if (!isNaN(n)) allTests[n] = ent.name;
  });

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

async function start(id) {
  if (id === undefined) {
    for (const i in allTests) {
      await test(allTests[i]);
    }
    summary();

  } else if (id in allTests) {
    await test(allTests[id]);
    summary();

  } else {
    error('Unknown test ID');
  }
}

async function test(name) {
  log('Testing', chalk.cyan(name), '...');
  const pipyProc = await startPipy(join(currentDir, name, 'main.js'));
  if (pipyProc) {
    try {
      let output, expected;
      log('Reading expected output...');
      try {
        expected = fs.readFileSync(join(currentDir, name, 'output'));
      } catch (e) {
        error('Failed reading expected output');
        throw e;
      }
      log('Running curl...');
      try {
        const buffer = [];
        await new Promise((resolve, reject) => {
          const subproc = execFile(
            join(currentDir, name, 'test.sh'), { encoding: 'buffer' },
            err => { if (err) reject(err); else resolve(); },
          );
          subproc.stdout.on('data', data => {
            process.stdout.write(data);
            buffer.push(data);
          });
        });
        output = Buffer.concat(buffer);
      } catch (e) {
        error('Failed running curl');
        log(e.message);
        throw e;
      }
      if (Buffer.compare(output, expected)) {
        testResults[name] = false;
        error('Test', name, 'failed with unexpected output:');
        log(output.toString());
        error(`Test ${name} failed!`);
      } else {
        testResults[name] = true;
        log(chalk.green(`Test ${name} OK.`));
      }
    } catch (e) {
    } finally {
      pipyProc.kill();
    }
  } else {
    error('Failed to start Pipy');
  }
}

async function startPipy(filename) {
  log('Starting Pipy...');
  log(pipyBinPath, filename);
  const proc = spawn(pipyBinPath, [filename]);
  const lineBuffer = [];
  let started = false, exited = false;
  proc.stderr.on('exit', () => exited = true);
  proc.stderr.on('data', data => {
    let i = 0, n = data.length;
    while (i < n) {
      let j = i;
      while (j < n && data[j] !== 10) j++;
      if (j > i) lineBuffer.push(data.slice(i, j));
      if (j < n) {
        const line = Buffer.concat(lineBuffer).toString();
        lineBuffer.length = 0;
        log(chalk.bgGreen('pipy >>>'), line);
        if (line.indexOf('Thread 0 started') >= 0) started = true;
      }
      i = j + 1;
    }
  });
  for (let i = 0; i < 10; i++) {
    if (started) return proc;
    if (exited) break;
    await sleep(1);
  }
  proc.kill();
  return null;
}

program
  .argument('[testcase-id]')
  .action(id => start(id))
  .parse(process.argv)
