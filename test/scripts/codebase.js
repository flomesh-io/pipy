import { spawn } from 'child_process';
import { join, dirname } from 'path';
import fs from 'fs';
import got from 'got';
import chalk from 'chalk';

const log = console.log;
const pipyBinPath = join(dirname(new URL(import.meta.url).pathname), '../../bin/pipy');

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

async function startRepo(codebaseName, basePath) {
  log('Starting repo...');

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

  for (let i = 0; i < 10 && !started; i++) {
    await new Promise(
      resolve => setTimeout(resolve, 1000)
    );
  }

  if (started) {
    log('Repo started');
  } else {
    log('Failed starting repo');
  }

  const codebasePath = join('api/v1/repo', codebaseName);
  const codebaseFilePath = join('api/v1/repo-files', codebaseName);

  const client = got.extend({
    prefixUrl: 'http://localhost:6060',
  });

  log('Creating codebase: ', codebaseName);
  try {
    await client.post(codebasePath, {
      json: {}
    });
  } catch (e) {
    log('Failed creating codebase');
  }

  async function uploadDir(dirName) {
    const dirPath = join(basePath, dirName);
    for (const name of fs.readdirSync(dirPath)) {
      const path = join(dirPath, name);
      if (fs.statSync(path).isDirectory()) {
        await uploadDir(join(dirName, name));
      } else {
        log('  Uploading', path);
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
    log('Failed uploading codebase');
  }

  log('Publishing codebase...');
  try {
    await client.patch(codebasePath, {
      json: { version: 2 }
    });
  } catch (e) {
    log('Failed publishing codebase');
  }

  return proc;
}

async function startWorker(url) {
  log('  Starting worker...');
  let started = false;
  const proc = startProcess(
    pipyBinPath, [url],
    line => {
      log(chalk.bgGreen('worker >>>'), line);
      if (line.indexOf('Listening on port') >= 0) {
        started = true;
      }
    }
  );

  for (let i = 0; i < 10 && !started; i++) {
    await new Promise(
      resolve => setTimeout(resolve, 1000)
    );
  }

  if (started) {
    log('Worker started');
  } else {
    log('Failed starting worker');
  }

  return proc;
}

export default async function(name, basePath) {
  log('Starting codebase', chalk.magenta(name), '...');
  const repo = await startRepo(`test/${name}`, basePath);
  const worker = await startWorker(`http://localhost:6060/repo/test/${name}/`);
  log('Codebase', chalk.magenta(name), 'started');
  return { repo, worker };
}
