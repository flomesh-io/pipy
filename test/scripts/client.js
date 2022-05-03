import got from 'got';
import chalk from 'chalk';
import prettyBytes from 'pretty-bytes';
import fs from 'fs';
import http from 'http';
import https from 'https';
import http2 from 'http2';
import { join } from 'path';

const log = console.log;
const spaces = new Array(100).fill(' ').join('');

function padding(text, width) {
  text = text.toString();
  const n = text.length;
  if (n >= width) return text;
  return text + spaces.substring(0, width - n);
}

const protocols = {};

protocols.http = function(target) {
  const agent = new http.Agent();

  const client = got.extend({
    prefixUrl: 'http://' + target,
    decompress: false,
    agent: {
      http: agent,
    },
  });

  const f = ({ path, ...req }) => client(path, req);
  f.destroy = () => agent.destroy();

  return f;
}

protocols.https = function(target) {
  const agent = new https.Agent({
    keepAlive: true,
  });

  const client = got.extend({
    prefixUrl: 'https://' + target,
    decompress: false,
    agent: {
      https: agent,
    },
    https: {
      rejectUnauthorized: false,
    },
  });

  const f = ({ path, ...req }) => client(path, req);
  f.destroy = () => agent.destroy();

  return f;
}

protocols.http2 = function(target) {
  const client = http2.connect('http://' + target);

  const f = ({ path, ...req }) => (
    new Promise(
      (resolve, reject) => {
        const body = [];
        const headers = { ':path': path };
        const response = {};
        if (req.headers) Object.assign(headers, req.headers);
        const r = client.request(headers);
        r.on('response', headers => Object.assign(response, headers));
        r.on('data', chunk => body.push(chunk));
        r.on('end', () => resolve({
          status: response[':status'],
          rawBody: Buffer.concat(body),
        }));
        r.on('error', err => reject(err));
        r.end();
      }
    )
  );

  f.destroy = () => client.destroy();

  return f;
}

export default async function(config, basePath) {
  const stats = {
    totalRequests: 0,
    totalTransfer: 0,
    totalStatusErrors: 0,
    totalVerifyErrors: 0,
  };

  const allRequests = {};

  for (const k in config.requests) {
    const { handler, ...options } = config.requests[k];
    let { method, path, body } = options;

    if (path.startsWith('/')) {
      path = path.substring(1);
    }

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

    options.path = path;
    options.body = body;

    if (handler) {
      const mod = await import(`./handlers/${handler}`);
      allRequests[k] = {
        request: mod.request(options),
        verify: mod.verify(options),
      };

    } else {
      allRequests[k] = options;
    }
  }

  async function session(client, requests, count) {
    let n = 0;
    while (n < count) {
      for (let req of requests) {
        let verify;
        if (req.request && req.verify) {
          verify = req.verify;
          req = req.request();
        }

        try {
          const res = await client(req);
          stats.totalTransfer += res.rawBody.length;
          if (res.statusCode < 400) {
            stats.totalRequests++;
          } else {
            stats.totalStatusErrors++;
          }
          if (verify) {
            const ok = verify(req, res);
            if (!ok) stats.totalVerifyErrors++;
          }
        } catch (err) {
          stats.totalStatusErrors++;
        }
        if (++n >= count) break;
      }
    }

    client.destroy();
  }

  async function thread(client, requests, options) {
    for (;;) {
      await session(client, requests, options.count);
    }
  }

  log('Starting client...');

  const threads = config.threads.map(
    ({ target, protocol, concurrency, requests, ...options }, i) => {
      concurrency = (concurrency|0) || 1;
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
      return { target, protocol, concurrency, requests, options };
    }
  );

  const createClient = (target, protocol) => {
    const p = protocols[protocol];
    if (!p) throw new Error('Unknown protocol: ' + protocol);
    return p(target);
  }

  for (const { target, protocol, concurrency, requests, options } of threads) {
    let client = null;
    for (let i = 0; i < concurrency; i++) {
      client = createClient(target, protocol || 'http');
      thread(client, requests, options);
    }
  }

  log('Client started');

  let time = Date.now();
  let tick = 0;
  setInterval(
    () => {
      tick++;
      const now = Date.now();
      const t = (now - time) / 1000;
      log(
        chalk.bgCyan('report >>>'),
        chalk.cyan(padding(tick + 's', 8)),
        'Requests'      , chalk.green(padding(stats.totalRequests, 8)),
        'Status Errors' , chalk.red(padding(stats.totalStatusErrors, 8)),
        'Verify Errors' , chalk.red(padding(stats.totalVerifyErrors, 8)),
        'Transfer'      , chalk.magenta(padding(prettyBytes(Math.floor(stats.totalTransfer/t)) + '/s', 8)),
      );
      stats.totalTransfer = 0;
      time = now;
    },
    1000
  );
}
