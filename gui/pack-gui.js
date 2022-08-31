const fs = require('fs');
const path = require('path');
const zlib = require('zlib');
const tar = require('tar-stream');
const pack = tar.pack();

const PUBLIC_PATH = path.join(__dirname, '../public');
const OUTPUT_PATH = path.normalize(process.argv[2]);

const {
  formatSize,
  writeBinaryHeaderFile,
} = require('./pack-utils.js');

function traverse(pathname) {
  const dirname = path.join(PUBLIC_PATH, pathname);
  const entries = fs.readdirSync(dirname, { withFileTypes: true });
  for (const ent of entries) {
    const name = ent.name;
    if (name[0] === '.') continue;
    if (name.endsWith('.map')) continue;
    const filename = pathname + '/' + name;
    if (ent.isDirectory()) {
      traverse(filename);
    } else {
      const data = fs.readFileSync(dirname + '/' + name);
      const gz = zlib.gzipSync(data);
      pack.entry({ name: filename + '.gz' }, gz);
      console.log('Packed', formatSize(data.length), '->', formatSize(gz.length), filename);
    }
  }
}

require('get-stream').buffer(pack).then(buffer => {
  console.log(`Compressing GUI tarball (size = ${formatSize(buffer.length)})...`);
  const compressed = zlib.brotliCompressSync(buffer, {
    params: {
      [zlib.constants.BROTLI_PARAM_QUALITY]: zlib.constants.BROTLI_MAX_QUALITY,
      [zlib.constants.BROTLI_PARAM_LGWIN]: zlib.constants.BROTLI_MAX_WINDOW_BITS,
    },
  })
  console.log(`GUI tarball size after compression: ${formatSize(compressed.length)}`);
  console.log(`Writing to ${OUTPUT_PATH}...`);
  writeBinaryHeaderFile(OUTPUT_PATH, 's_gui_tar', compressed);
});

traverse('.');

pack.finalize();
