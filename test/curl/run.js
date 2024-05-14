#!/usr/bin/env node

import os from 'os';
import fs from 'fs';
import url from 'url';
import chalk from 'chalk';

import { spawn, execFile } from 'child_process';
import { join, dirname } from 'path';
import { program } from 'commander';

const log = console.log;
const error = (...args) => log.apply(this, [chalk.bgRed('ERROR')].concat(args.map(a => chalk.red(a))));
const sleep = (t) => new Promise(resolve => setTimeout(resolve, t * 1000));
const currentDir = dirname(url.fileURLToPath(import.meta.url));
const testScriptName = os.platform() === 'win32' ? 'test.cmd' : 'test.sh';
const pipyBinName = os.platform() === 'win32' ? '..\\..\\bin\\Release\\pipy.exe' : '../../bin/pipy';
const pipyBinPath = join(currentDir, pipyBinName);
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
            join(currentDir, name, testScriptName), { encoding: 'buffer' },
            err => { if (err) reject(err); else resolve(); },
          );
          subproc.stdout.on('data', data => {
            process.stdout.write(data);
            buffer.push(data);
          });
          subproc.on('exit', () => resolve());
        });
        output = Buffer.concat(buffer);
      } catch (e) {
        error('Failed running curl');
        log(e.message);
        throw e;
      }
      const a = expected.toString().replaceAll('\r\n', '\n');
      const b = output.toString().replaceAll('\r\n', '\n');
      if (a !== b) {
        testResults[name] = false;
        error('Test', name, 'failed with unexpected output:');
        log(output.toString());
        error(`Test ${name} failed!`);
      } else {
        testResults[name] = true;
        log(chalk.green(`Test ${name} OK.`));
      }
    } catch (e) {
      testResults[name] = false;
    } finally {
      pipyProc.kill();
    }
  } else {
    testResults[name] = false;
    error('Failed to start Pipy');
  }
}

async function startPipy(filename) {
  log('Starting Pipy...');
  log(pipyBinPath, filename);
  const proc = spawn(pipyBinPath, [filename, '--log-level=debug:thread']);
  const lineBuffer = [];
  let started = false;
  return await Promise.race([
    new Promise(
      resolve => (
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
              if (line.indexOf('Thread 0 started') >= 0) {
                started = true;
                resolve(proc);
              }
            }
            i = j + 1;
          }
        })
      )
    ),
    sleep(10).then(() => {
      if (!started) {
        proc.kill();
        return null;
      }
    }),
  ]);
}

program
  .argument('[testcase-id]')
  .action(id => start(id))
  .parse(process.argv)
