import got from 'got';
import chalk from 'chalk';
import prettyBytes from 'pretty-bytes';
import fs from 'fs';
import http from 'http';
import { join } from 'path';

const log = console.log;
const spaces = new Array(100).fill(' ').join('');

function padding(text, width) {
  text = text.toString();
  const n = text.length;
  if (n >= width) return text;
  return text + spaces.substr(0, width - n);
}

export default async function(config, basePath) {
  const stats = {
    totalRequests: 0,
    totalTransfer: 0,
    totalErrors: 0,
  };

  const allRequests = {};

  for (const k in config.requests) {
    let {
      method,
      path,
      headers,
      body,
    } = config.requests[k];

    if (method === 'POST') {
      const { file, text, repeat } = body;
      if (file) {
        body = fs.readFileSync(join(basePath, file));
      } else if (text) {
        body = new Array(repeat || 1).fill(text).join('');
      } else {
        throw new Error('Invalid request body');
      }
    }

    if (path.startsWith('/')) {
      path = path.substring(1);
    }

    const req = { method, path };
    if (headers) req.headers = headers;
    if (body) req.body = body;

    allRequests[k] = req;
  }

  async function session(target, requests, count, minIdle, maxIdle) {
    const agent = new http.Agent({
      keepAlive: true,
    });

    const client = got.extend({
      prefixUrl: `http://${target}`,
      agent: {
        http: agent,
      },
    });

    let n = 0;
    while (n < count) {
      for (const req of requests) {
        const { path, ...options } = req;
        try {
          const res = await client(path, options);
          stats.totalTransfer += res.rawBody.length;
          if (res.statusCode < 400) {
            stats.totalRequests++;
          } else {
            stats.totalErrors++;
          }
        } catch (err) {
          stats.totalErrors++;
        }
        const idle = Math.floor(Math.random() * (maxIdle - minIdle)) + minIdle;
        if (idle > 0) await new Promise(resolve => setTimeout(resolve, idle));
        if (++n >= count) break;
      }
    }

    agent.destroy();
  }

  async function thread(target, requests, options) {
    for (;;) {
      const min = options.minRequests || 1;
      const max = options.maxRequests || 1;
      const count = Math.floor(Math.random() * (max - min)) + min;
      const minIdle = options.minIdle || 0;
      const maxIdle = options.maxIdle || 0;
      await session(target, requests, count, minIdle, maxIdle);
    }
  }

  log('Starting client...');

  const threads = config.threads.map(
    ({ target, concurrency, requests, ...options }, i) => {
      concurrency = concurrency || 1;
      log(`  Thread group #${i}:`,
        'Threads'     , chalk.magenta(padding(concurrency, 8)),
        'Pattern size', chalk.magenta(padding(requests.length, 8)),
        'Target'      , target,
      );
      requests = requests.map(
        name => {
          const req = allRequests[name];
          if (!req) throw new Error(`Name of request not found: ${name}`);
          return req;
        }
      );
      return { target, concurrency, requests, options };
    }
  );

  for (const { target, concurrency, requests, options } of threads) {
    for (let i = 0; i < concurrency; i++) {
      thread(target, requests, options);
    }
  }

  log('Client started');

  let time = Date.now();
  setInterval(
    () => {
      const now = Date.now();
      const t = (now - time) / 1000;
      log(
        'Requests', chalk.green(padding(stats.totalRequests, 8)),
        'Errors'  , chalk.red(padding(stats.totalErrors, 8)),
        'Transfer', chalk.magenta(padding(prettyBytes(Math.floor(stats.totalTransfer/t)) + '/s', 8)),
      );
      stats.totalTransfer = 0;
      time = now;
    },
    1000
  );
}
