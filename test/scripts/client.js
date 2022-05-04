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
  const agent = new http.Agent({
    keepAlive: true,
    scheduling: 'fifo',
  });

  const client = got.extend({
    prefixUrl: 'http://' + target,
    decompress: false,
    throwHttpErrors: false,
    agent: {
      http: agent,
    },
  });

  const f = ({ path, ...req }) => {
    if (path.startsWith('/')) {
      path = path.substring(1);
    }
    return client(path, req);
  }

  f.destroy = () => agent.destroy();

  return f;
}

protocols.https = function(target) {
  const agent = new https.Agent({
    keepAlive: true,
    scheduling: 'fifo',
  });

  const client = got.extend({
    prefixUrl: 'https://' + target,
    decompress: false,
    throwHttpErrors: false,
    agent: {
      https: agent,
    },
    https: {
      rejectUnauthorized: false,
    },
  });

  const f = ({ path, ...req }) => {
    if (path.startsWith('/')) {
      path = path.substring(1);
    }
    return client(path, req);
  }

  f.destroy = () => agent.destroy();

  return f;
}

protocols.http2 = function(target) {
  const client = http2.connect('http://' + target);

  let error = null;
  client.on('error', err => {
    error = err;
    console.log(err);
  });

  const f = ({ method, path, body, ...req }) => (
    new Promise(
      (resolve, reject) => {
        if (error) {
          reject(error);
          return;
        }
        const buffer = [];
        const headers = {
          ':method': method,
          ':path': path,
        };
        const response = {};
        if (req.headers) Object.assign(headers, req.headers);
        const r = client.request(headers);
        r.on('response', headers => Object.assign(response, headers));
        r.on('data', chunk => buffer.push(chunk));
        r.on('end', () => (
          resolve({
            statusCode: response[':status'],
            rawBody: Buffer.concat(buffer),
          })
        ));
        r.on('error', err => reject(err));
        r.on('frameError', () => reject('HTTP/2 Frame Error'));
        r.end(method === 'POST' ? body : undefined);
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
    const { method, body } = options;

    if (method === 'POST') {
      const { file, text, repeat } = body;
      if (file) {
        options.body = fs.readFileSync(join(basePath, file));
      } else if (text) {
        options.body = new Array(repeat || 1).fill(text).join('');
      } else {
        throw new Error('Invalid request body');
      }
    }

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

        if (++n >= count) break;
      }
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
    const multiplexedClient = (
      protocol === 'http2' ? createClient(target, protocol) : null
    );
    for (let i = 0; i < concurrency; i++) {
      (async () => {
        for (;;) {
          const client = multiplexedClient || createClient(target, protocol || 'http');
          await session(client, requests, options.count);
          if (!multiplexedClient) client.destroy();
        }
      })();
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
