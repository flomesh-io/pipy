const fs = require('fs');
const path = require('path');
const zlib = require('zlib');
const tar = require('tar-stream');
const pack = tar.pack();

const ROOT_PATH = path.join(__dirname, '..');
const TUTORIAL_PATH = path.join(ROOT_PATH, 'tutorial');
const SAMPLES_PATH = path.join(ROOT_PATH, 'samples');
const OUTPUT_PATH = path.normalize(process.argv[2]);
const CUSTOM_CODEBASES = process.argv[3] || '';

const {
  formatSize,
  listFilenames,
  writeBinaryHeaderFile,
} = require('./pack-utils.js');

const dirnames = [].concat(
  fs.readdirSync(TUTORIAL_PATH, { withFileTypes: true })
    .filter(e => e.isDirectory() && !e.name.startsWith('.'))
    .sort().map(e => `tutorial/${e.name}`),
  fs.readdirSync(SAMPLES_PATH, { withFileTypes: true })
    .filter(e => e.isDirectory() && !e.name.startsWith('.'))
    .filter(e => e.name !== 'nmi' && e.name !== 'bpf' && e.name !== 'webapp')
    .sort().map(e => `samples/${e.name}`),
);

const codebases = [];

function addCodebase(name, dirname) {
  const dirpath = path.resolve(ROOT_PATH, dirname);
  const filenames = listFilenames(dirpath);
  const files = {};
  for (const name of filenames) {
    let content = fs.readFileSync(path.join(dirpath, name), 'utf8');
    if (content !== null) {
      files[name] = content;
      console.log('  ' + name);
    }
  }
  codebases.push({ name, files });
}

dirnames.forEach(dirname => {
  console.log(`Codebase ${dirname}:`);
  addCodebase(dirname, dirname);
});

if (CUSTOM_CODEBASES) {
  CUSTOM_CODEBASES.split(',').forEach(
    item => {
      const [key, path] = item.split(':');
      if (!key || !path) return;
      const [group, name] = key.split('/');
      if (!group || !name) return;
      const fullname = `${group}/${name}`;
      console.log(`Codebase ${fullname}:`);
      addCodebase(fullname, path);
    }
  );
}

require('get-stream').buffer(pack).then(buffer => {
  const data = zlib.gzipSync(buffer);
  console.log(`Codebase tarball size: ${formatSize(data.length)}`);
  console.log(`Writing to ${OUTPUT_PATH}...`);
  writeBinaryHeaderFile(OUTPUT_PATH, 's_codebases_tar_gz', data);
});

codebases.forEach(
  ({ name, files }) => {
    for (const filename in files) {
      pack.entry({ name: `${name}/${filename}` }, files[filename]);
    }
  }
);

pack.finalize();
