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
  proc.stdout.on('data', data => {
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
}

async function startRepo(codebaseName, basePath) {
  log('Starting repo...');
  await new Promise(
    resolve => {
      startProcess(
        pipyBinPath, [],
        line => {
          log(chalk.bgGreen('repo >>>'), line);
          if (line.indexOf('Listening on port') >= 0) {
            resolve();
          }
        }
      );
    }
  );
  log('Repo started');

  const codebasePath = join('api/v1/repo', codebaseName);

  const client = got.extend({
    prefixUrl: 'http://localhost:6060',
  });

  log('Creating codebase: ', codebaseName);
  await client.post(codebasePath, {
    json: {}
  });

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
          join(codebasePath, dirName, name),
          { body }
        );
      }
    }
  }

  await uploadDir('/');

  log('Publishing codebase...');
  await client.post(codebasePath, {
    json: { version: 2 }
  });
}

async function startWorker(url) {
  log('  Starting worker...');
  await new Promise(
    resolve => {
      startProcess(
        pipyBinPath, [url],
        line => {
          log(chalk.bgGreen('worker >>>'), line);
          if (line.indexOf('Listening on port') >= 0) {
            resolve();
          }
        }
      );
    }
  );
  log('Worker started');
}

export default async function(name, basePath) {
  log('Starting codebase', chalk.magenta(name), '...');
  await startRepo(`test/${name}`, basePath);
  await startWorker(`http://localhost:6060/repo/test/${name}/`);
  log('Codebase', chalk.magenta(name), 'started');
}
