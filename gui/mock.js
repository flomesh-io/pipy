const Koa = require('koa');
const bodyParser = require('koa-bodyparser');

const config = require('./mock-config.json');

const files = {
  'pipy.js': 'pipy()',
  'conf.json': '{}',
  'README.md': '# Welcome',
  'modules': {
    'foo': 'foo',
    'bar': 'bar',
  },
};

const app = new Koa();

let currentRunningProgram = '';

app.use(bodyParser({
  enableTypes: [ 'text' ],
}))

app.use(ctx => {
  switch (ctx.path) {
    case '/api/files':
      ctx.response.set('content-type', 'application/json');
      ctx.body = JSON.stringify(
        files,
        (_, v) => typeof v === 'string' ? '' : v,
      );
      break;

    case '/api/program':
      switch (ctx.method) {
        case 'GET':
          ctx.body = currentRunningProgram;
          ctx.response.set('content-type', 'text/plain');
          break;
        case 'POST':
          if (typeof ctx.request.body === 'string') {
            currentRunningProgram = ctx.request.body;
            ctx.status = 201;
          } else {
            ctx.status = 400;
          }
          break;
        case 'DELETE':
          currentRunningProgram = '';
          ctx.status = 204;
          break;
      }
      break;

    case '/api/config':
      ctx.body = JSON.stringify(config);
      ctx.response.set('content-type', 'application/json');
      break;

    case '/api/graph':
      ctx.body = JSON.stringify(config['/http-reverse-proxy.js']);
      ctx.response.set('content-type', 'application/json');
      break;

    case '/api/log':
      let size = ctx.request.get('x-log-size')|0;
      if (currentRunningProgram) {
        size++;
        ctx.response.set('x-log-size', size);
        ctx.body = `Line ${size}\n`;
      } else {
        ctx.response.set('x-log-size', size);
        ctx.body = '';
      }
      break;

    default:
      if (ctx.path.startsWith('/api/files/')) {
        const segs = ctx.path.split('/').slice(3);
        let dir = files;
        while (dir && segs.length > 1) {
          dir = dir[segs.shift()];
        }
        const name = segs[0];
        if (!dir) {
          ctx.status = 404;
        } else if (ctx.method === 'GET') {
          const file = dir[name];
          if (typeof file !== 'string') {
            ctx.status = 404;
          } else {
            ctx.body = file;
            ctx.response.set('content-type', 'text/plain');
          }
        } else if (ctx.method === 'DELETE') {
          const file = dir[name];
          if (typeof file !== 'string') {
            ctx.status = 404;
          } else {
            delete dir[name];
            ctx.status = 204;
          }
        } else if (ctx.method === 'POST') {
          const body = ctx.request.body;
          if (typeof body !== 'string') {
            ctx.status = 400;
          } else {
            dir[name] = body;
            ctx.status = 201;
          }
        } else {
          ctx.status = 405;
        }
      }
      break;
  }
});

app.listen(6060);