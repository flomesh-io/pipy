const fs = require('fs');
const path = require('path');
const zlib = require('zlib');
const tar = require('tar-stream');
const pack = tar.pack();

const ROOT_PATH = path.join(__dirname, '..');
const TUTORIAL_PATH = path.join(ROOT_PATH, 'tutorial');
const SAMPLES_PATH = path.join(ROOT_PATH, 'samples');
const OUTPUT_PATH = path.normalize(process.argv[2]);

const {
  formatSize,
  writeBinaryHeaderFile,
} = require('./pack-utils.js');

const dirnames = [].concat(
  fs.readdirSync(TUTORIAL_PATH).sort().map(s => `tutorial/${s}`),
  fs.readdirSync(SAMPLES_PATH).sort().map(s => `samples/${s}`),
);

const codebases = [];

function listFilenames(dirpath, base, filenames) {
  const names = fs.readdirSync(dirpath);
  for (const name of names) {
    const abspath = path.join(dirpath, name);
    const st = fs.statSync(abspath);
    if (st.isFile()) {
      filenames.push(base + name);
    } else if (st.isDirectory()) {
      listFilenames(abspath, base + name + '/', filenames);
    }
  }
}

dirnames.forEach(dirname => {
  console.log(`Codebase ${dirname}:`);
  const dirpath = path.join(ROOT_PATH, dirname);
  const filenames = [];
  const files = {};
  listFilenames(dirpath, '', filenames)
  for (const name of filenames) {
    let content = fs.readFileSync(path.join(dirpath, name), 'utf8');
    if (content !== null) {
      files[name] = content;
      console.log('  ' + name);
    }
  }
  codebases.push({ name: dirname, files });
});

require('get-stream').buffer(pack).then(buffer => {
  const data = zlib.gzipSync(buffer);
  console.log(`Tutorial tarball size: ${formatSize(data.length)}`);
  console.log(`Writing to ${OUTPUT_PATH}...`);
  writeBinaryHeaderFile(OUTPUT_PATH, 's_samples_tar_gz', data);
});

codebases.forEach(
  ({ name, files }) => {
    for (const filename in files) {
      pack.entry({ name: `${name}/${filename}` }, files[filename]);
    }
  }
);

pack.finalize();
