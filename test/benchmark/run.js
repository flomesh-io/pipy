#!/usr/bin/env node

import os from 'os';
import fs from 'fs';
import url from 'url';
import got from 'got';
import si from 'systeminformation';
import chalk from 'chalk';

import { spawn } from 'child_process';
import { join, dirname } from 'path';
import { program } from 'commander';

const log = console.log;
const error = (...args) => log.apply(this, [chalk.bgRed('ERROR')].concat(args.map(a => chalk.red(a))));
const sleep = (t) => new Promise(resolve => setTimeout(resolve, t * 1000));

const currentDir = dirname(url.fileURLToPath(import.meta.url));
const binName = os.platform() === 'win32' ? '..\\..\\bin\\Release' : '../../bin';
const pipyExe = os.platform() === 'win32' ? 'pipy.exe' : 'pipy';
const baselineExe = os.platform() === 'win32' ? 'baseline.exe' : 'baseline';
const binPath = join(currentDir, binName);
const allTests = [];
const testResults = {};
const testResultVariances = {};

fs.readdirSync(currentDir, { withFileTypes: true })
  .filter(ent => ent.isDirectory() && ent.name !== 'baseline' && ent.name !== 'stress')
  .forEach(ent => {
    const n = parseInt(ent.name.substring(0, 3));
    if (!isNaN(n)) allTests[n] = ent.name;
  });

async function summary() {
  const sysinfo = [];
  const collectSysinfo = (info, depth) => {
    depth = depth || 0;
    for (const k in info) {
      const v = info[k];
      const label = '  '.repeat(depth) + k;
      if (typeof v === 'object') {
        sysinfo.push([ label, '' ]);
        collectSysinfo(v, depth + 1);
      } else {
        const value = v.toString();
        if (value && value.length < 50) sysinfo.push([ label, value ]);
      }
    }
  };

  collectSysinfo({ OS: await si.osInfo() });
  collectSysinfo({ CPU: await si.cpu() });

  const width = 30 + Math.max(
    Math.max.apply(null, sysinfo.map(([k, v]) => k.length + v.length)),
    Math.max.apply(null, Object.keys(testResults).map(name => name.length)),
  );

  log('='.repeat(width));
  log('System information');
  log('-'.repeat(width));

  sysinfo.forEach(
    ([k, v]) => log([
      k,
      ' '.repeat(width - k.length - v.length),
      v,
    ].join(''))
  );

  log('='.repeat(width));
  log('Summary');
  log('-'.repeat(width));

  const maxResult = (testResults['baseline'] | 0) || (
    Math.max.apply(null, Object.values(testResults).map(n => n|0))
  );

  Object.keys(testResults).sort().forEach(
    name => {
      const result = testResults[name]|0;
      const count = (result).toString() + ' (±' + (Math.sqrt(testResultVariances[name])/result*100).toFixed(2) + '%)';
      const ratio = (result / maxResult * 100).toFixed(2);
      const columns = [
        name,
        ' '.repeat(width - 10 - name.length - count.length),
        chalk.green(count),
        ' '.repeat(10 - ratio.length - 1),
        chalk.cyan(ratio + '%'),
      ];
      log(columns.join(''));
    }
  );
  log('='.repeat(width));
}

async function start(id) {
  const procs = [];

  try {
    log('Starting', chalk.magenta('mock'), '...');
    procs.push(await startPipy([ join(currentDir, 'stress/mock.js') ]));

    log('Starting', chalk.magenta('baseline'), '...');
    procs.push(await startBaseline());

    if (id === undefined) {
      for (const i in allTests) {
        const name = allTests[i];
        const port = 8000 + (i|0);
        const path = join(currentDir, name, 'main.js');
        log('Starting', chalk.magenta(name), '...');
        procs.push(await startPipy([ path ], { LISTEN: `0.0.0.0:${port}` }));
      }

      await benchmark('baseline', 8000);

      for (const i in allTests) {
        const name = allTests[i];
        const port = 8000 + (i|0);
        await benchmark(name, port);
      }

      await summary();

    } else if (id in allTests) {
      const name = allTests[id];
      const path = join(currentDir, name, 'main.js');
      log('Starting', chalk.magenta(name), '...');
      procs.push(await startPipy([ path ], { LISTEN: '0.0.0.0:8001' }));
      await benchmark('baseline', 8000);
      await benchmark(name, 8001);
      await summary();

    } else {
      error('Unknown test ID');
    }

  } catch (e) {
    error(e.message);
    log(e);

  } finally {
    for (const proc of procs) {
      proc.kill();
    }
  }
}

async function wait(time, msg) {
  for (let i = 0; i < time; i++) {
    log(`${msg} for ${time-i}s...`);
    await sleep(1);
  }
}

async function measure(name, time) {
  const client = got.extend({
    prefixUrl: 'http://localhost:6060',
  });

  const getCount = () => client.get('metrics').then(
    res => /^counts ([0-9]+)$/m.exec(res.body)?.[1] | 0
  );

  let lastCount = await getCount();
  log(`Measure ${time} times...`);
  await sleep(1);

  const samples = [];
  for (let i = 0; i < time; i++) {
    const count = await getCount();
    const delta = count - lastCount;
    samples.push(delta);
    log(`Count = ${count}, Increment = ${delta}`);
    lastCount = count;
    await sleep(1);
  }

  const max = Math.max.apply(null, samples);
  const min = Math.min.apply(null, samples);
  const average = samples.reduce((a, b) => a + b) / samples.length;
  const variance = samples.map(v => (v - average) * (v - average)).reduce((a, b) => a + b) / samples.length;

  log('Result' +
    ': maximum =', chalk.green(max) +
    ', minimum =', chalk.green(min) +
    ', average =', chalk.green(average + ' (±' + (Math.sqrt(variance) / average * 100).toFixed(2) + '%)')
  );

  testResults[name] = average;
  testResultVariances[name] = variance;
}

async function benchmark(name, port) {
  log('Benchmarking', chalk.magenta(name), '...');
  await wait(10, 'Cool down');

  let proc;
  try {
    const args = [
      join(currentDir, 'stress/main.js'),
      '--no-graph',
      '--admin-port=6060',
      '--threads=2',
    ];

    const env = {
      URL: `http://localhost:${port}/`,
      METHOD: 'GET',
      PAYLOAD_SIZE: 0,
      CONCURRENCY: 100,
    };

    proc = await startPipy(args, env);
    await wait(3, 'Warm up');
    await measure(name, 10);

    log('Benchmark', chalk.magenta(name), 'done');

  } finally {
    if (proc) proc.kill();
  }
}

async function startProcess(bin, args, env, label, startLine) {
  const proc = spawn(bin, args, { env });
  const cmdline = `${bin} ${args.join(' ')}`;
  log(cmdline);
  let started = false;
  return await Promise.race([
    new Promise(
      (resolve, reject) => {
        const lineBuffer = [];
        const collectOutput = data => {
          if (!started) {
            let i = 0, n = data.length;
            while (i < n) {
              let j = i;
              while (j < n && data[j] !== 10) j++;
              if (j > i) lineBuffer.push(data.slice(i, j));
              if (j < n) {
                const line = Buffer.concat(lineBuffer).toString();
                lineBuffer.length = 0;
                log(chalk.bgGreen(label + ' >>>'), line);
                if (line.indexOf(startLine) >= 0) {
                  started = true;
                  resolve(proc);
                  return;
                }
              }
              i = j + 1;
            }
          }
        };
        proc.stderr.on('data', collectOutput);
        proc.stdout.on('data', collectOutput);
        proc.on('exit', () => reject(new Error(`Failed to start ${cmdline}`)));
      }
    ),
    sleep(10).then(() => {
      if (!started) {
        proc.kill();
        throw new Error(`Failed to start ${cmdline}`);
      }
    }),
  ]);
}

function startBaseline() {
  return startProcess(
    join(binPath, baselineExe), [], {},
    'baseline', 'Listening on port',
  );
}

function startPipy(args, env) {
  return startProcess(
    join(binPath, pipyExe), args, env,
    'pipy', 'Thread 0 started',
  );
}

program
  .argument('[testcase-id]')
  .action(id => start(id))
  .parse(process.argv)
