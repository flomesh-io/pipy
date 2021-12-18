import express from 'express';
import fs from 'fs';
import { join } from 'path';

const log = console.log;

export default async function(config, basePath) {
  const app = express();

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
