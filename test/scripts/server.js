import express from 'express';
import http2 from 'http2';
import fs from 'fs';
import { join } from 'path';

const log = console.log;

class Response {
  constructor(stream) {
    this._stream = stream;
    this._headers = {};
  }

  set(headers) {
    Object.assign(this._headers, headers);
    return this;
  }

  status(code) {
    this._headers[':status'] = code;
    return this;
  }

  send(content) {
    const s = this._stream;
    if (!s.closed) s.respond(this._headers);
    if (!s.closed) s.end(content);
  }
}

function createHTTP2() {
  const server = http2.createServer({
    peerMaxConcurrentStreams: 1000,
  });

  const routes = {
    GET: {},
    POST: {},
  };

  server.on('stream', (stream, headers) => {
    const method = headers[':method'];
    const path = headers[':path'];
    const cb = routes[method]?.[path];
    if (cb) {
      const buffer = [];
      stream.on('data', chunk => buffer.push(chunk));
      stream.on('error', err => log(err));
      stream.on('end', () => {
        const req = {
          method,
          path,
          headers: { ...headers },
          rawBody: Buffer.concat(buffer),
        };
        cb(req, new Response(stream));
      });
    } else {
      if (!stream.closed) stream.respond({ ':status': 404 });
      if (!stream.closed) stream.end();
    }
  });

  server.on('error', err => log(err));

  return {
    get: (path, cb) => routes.GET[path] = cb,
    post: (path, cb) => routes.POST[path] = cb,
    listen: port => server.listen(port),
  };
}

export default async function(config, basePath) {
  const app = config.protocol === 'http2' ? createHTTP2() : express();

  log('Starting mock server...');

  for (const k in config.endpoints) {
    const [method, path] = k.split(' ');
    const {handler, ...options} = config.endpoints[k];
    const {status, headers, file, text, repeat} = options;

    log('  Endpoint', method, path);

    let cb;

    if (handler) {
      const mod = await import(`./handlers/${handler}`);
      cb = mod.response(options);
    } else if (file) {
      const content = fs.readFileSync(join(basePath, file));
      cb = (_, res) => {
        if (headers) res.set(headers);
        res.status(status || 200).send(content);
      };
    } else if (text) {
      const content = new Array(repeat || 1).fill(text).join('');
      cb = (_, res) => {
        if (headers) res.set(headers);
        res.status(status || 200).send(content);
      };
    } else {
      throw new Error('Invalid response');
    }

    switch (method) {
      case 'GET': app.get(path, cb); break;
      case 'POST': app.post(path, cb); break;
      default: throw new Error('Invalid method');
    }
  }

  app.listen((config.listen|0) || 8080);

  log('Mock server started');
}
