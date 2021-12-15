#!/usr/bin/env node

import startCodebase from './scripts/codebase.js';
import startServer from './scripts/server.js';
import startClient from './scripts/client.js';

import fs from 'fs';
import YAML from 'yaml';

import { join, dirname } from 'path';
import { program } from 'commander';

async function start(testcase, options) {
  const basePath = join(dirname(new URL(import.meta.url).pathname), testcase);
  const config = YAML.parse(fs.readFileSync(join(basePath, 'plan.yaml'), 'utf8'));

  let {
    pipy,
    client,
    server,
  } = options;

  if (!pipy && !client && !server) {
    pipy = true;
    client = true;
    server = true;
  }

  if (pipy) await startCodebase(testcase, basePath);
  if (server) await startServer(config.server, basePath);
  if (client) await startClient(config.client, basePath, options.target);
}

const options = program
  .argument('<testcase>')
  .option('-p, --pipy', 'Run the Pipy codebase under testing')
  .option('-c, --client', 'Run the test client')
  .option('-s, --server', 'Run the mock server')
  .action((testcase, options) => start(testcase, options))
  .parse(process.argv)
