const fs = require('fs');
const path = require('path');
const zlib = require('zlib');
const tar = require('tar-stream');
const pack = tar.pack();

const TUTORIAL_PATH = path.join(__dirname, '../tutorial');
const OUTPUT_PATH = path.normalize(process.argv[2]);

const {
  formatSize,
  writeBinaryHeaderFile,
} = require('./pack-utils.js');

const dirnames = fs.readdirSync(TUTORIAL_PATH).sort();
const codebases = [];

dirnames.forEach(dirname => {
  console.log(`Codebase ${dirname}:`);
  const dirpath = path.join(TUTORIAL_PATH, dirname);
  const filenames = fs.readdirSync(dirpath);
  const files = {};
  for (const name of filenames) {
    let content = fs.readFileSync(path.join(dirpath, name), 'utf8');
    for (let i = codebases.length - 1; i >= 0; i--) {
      if (codebases[i].files[name] === content) {
        content = null;
        break;
      }
    }
    if (content !== null) {
      files[name] = content;
      console.log('  ' + name);
    }
  }
  codebases.push({ name: dirname, files });
});

require('get-stream').buffer(pack).then(buffer => {
  const data = zlib.gzipSync(buffer)
  console.log(`Tutorial tarball size: ${formatSize(data.length)}`);
  console.log(`Writing to ${OUTPUT_PATH}...`);
  writeBinaryHeaderFile(OUTPUT_PATH, 's_tutorial_tar_gz', data);
});

codebases.forEach(
  ({ name, files }) => {
    for (const filename in files) {
      pack.entry({ name: `${name}/${filename}` }, files[filename]);
    }
  }
);

pack.finalize();
