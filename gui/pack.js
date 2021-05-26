const fs = require('fs');
const path = require('path');
const zlib = require('zlib');
const tar = require('tar-stream');
const pack = tar.pack();

const BASE_PATH = path.join(__dirname, 'public');
const OUTPUT_PATH = path.normalize(process.argv[2]);

function formatSize(size) {
  const s = Math.round(size / 1024).toString();
  return ' '.repeat(6 - s.length) + s + 'K';
}

function traverse(pathname) {
  const dirname = path.join(BASE_PATH, pathname);
  const entries = fs.readdirSync(dirname, { withFileTypes: true });
  for (const ent of entries) {
    const name = ent.name;
    if (name[0] === '.') continue;
    if (name.endsWith('.map')) continue;
    const filename = pathname + '/' + name;
    if (ent.isDirectory()) {
      traverse(filename);
    } else {
      // const data = fs.readFileSync(dirname + '/' + name);
      // const br = zlib.brotliCompressSync(data, {
      //   params: {
      //     [zlib.constants.BROTLI_PARAM_QUALITY]: zlib.constants.BROTLI_MAX_QUALITY,
      //     [zlib.constants.BROTLI_PARAM_LGWIN]: zlib.constants.BROTLI_MAX_WINDOW_BITS,
      //   },
      // })
      // pack.entry({ name: filename + '.br' }, br);
      // console.log('Packed', formatSize(data.length), '->', formatSize(br.length), filename);
      const data = fs.readFileSync(dirname + '/' + name);
      const gz = zlib.gzipSync(data);
      pack.entry({ name: filename + '.gz' }, gz);
      console.log('Packed', formatSize(data.length), '->', formatSize(gz.length), filename);
    }
  }
}

require('get-stream').buffer(pack).then(buffer => {
  console.log(`GUI tarball size: ${formatSize(buffer.length)}`);
  console.log(`Writing to ${OUTPUT_PATH}...`);
  const fd = fs.openSync(OUTPUT_PATH, 'w+');
  fs.writeSync(fd, `static unsigned char s_gui_tar[${buffer.length}] = {\n`);
  for (let i = 0, n = buffer.length; i < n; i += 16) {
    const line = [];
    for (let j = 0; j < 16 && i + j < n; j++) {
      const b = buffer[i + j].toString(16);
      line.push(b.length > 1 ? '0x' + b : '0x0' + b);
    }
    fs.writeSync(fd, '  ' + line.join(', ') + ',\n');
  }
  fs.writeSync(fd, '};\n');
  fs.closeSync(fd);
});

traverse('.');

pack.finalize();
