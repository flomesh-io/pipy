import express from 'express';
import fs from 'fs';
import { join } from 'path';

const log = console.log;

export default function(config, basePath) {
  const app = express();

  log('Starting mock server...');

  for (const k in config.endpoints) {
    const [method, path] = k.split(' ');
    const {status, headers, file, text, repeat} = config.endpoints[k];

    log('  Endpoint', method, path);

    let handler;

    if (file) {
      const body = fs.readFileSync(join(basePath, file));
      handler = (_, res) => {
        if (headers) res.set(headers);
        res.status(status || 200).send(body);
      };
    } else if (text) {
      const body = new Array(repeat || 1).fill(text).join('');
      handler = (_, res) => {
        if (headers) res.set(headers);
        res.status(status || 200).send(body);
      };
    } else {
      throw new Error('Invalid response');
    }

    switch (method) {
      case 'GET': app.get(path, handler); break;
      case 'POST': app.post(path, handler); break;
      default: throw new Error('Invalid method');
    }
  }

  app.listen(config.listen || 8080);

  log('Mock server started');
}
