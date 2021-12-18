import { join } from 'path';

export function request(options) {
  const basePath = options.path;
  return function() {
    const n = Math.floor(Math.random() * 1e6);
    const path = join(basePath, n.toString());
    return { ...options, path };
  }
}

export function response(options) {
  const {
    status,
    headers,
    text,
    repeat,
  } = options;

  let content = '';
  if (text) {
    content = new Array(repeat || 1).fill(text).join('');
  }

  return function(req, res) {
    if (headers) res.set(headers);
    const body = req.path + '\n' + content;
    res.status(status || 200).send(body);
  }
}

export function verify() {
  return function(req, res) {
    const { path } = req;
    return (res.rawBody.slice(1, path.length + 1).toString() === path);
  }
}
