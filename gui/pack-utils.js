const fs = require('fs');
const path = require('path');

function listFilenames(dirpath, base, filenames) {
  const names = fs.readdirSync(dirpath);
  for (const name of names) {
    const abspath = path.join(dirpath, name);
    const st = fs.statSync(abspath);
    if (st.isFile()) {
      if (name.endsWith('.md') && name !== 'README.md') continue;
      filenames.push(base + name);
    } else if (st.isDirectory()) {
      listFilenames(abspath, base + name + '/', filenames);
    }
  }
}

module.exports = {
  formatSize: size => {
    const s = Math.round(size / 1024).toString();
    return ' '.repeat(6 - s.length) + s + 'K';
  },

  listFilenames: (dirpath) => {
    const filenames = [];
    listFilenames(dirpath, '', filenames);
    return filenames;
  },

  writeBinaryHeaderFile: (filename, name, data) => {
    const fd = fs.openSync(filename, 'w+');
    fs.writeSync(fd, `static unsigned char ${name}[${data.length}] __attribute__((aligned(16))) = {\n`);
    for (let i = 0, n = data.length; i < n; i += 16) {
      const line = [];
      for (let j = 0; j < 16 && i + j < n; j++) {
        const b = data[i + j].toString(16);
        line.push(b.length > 1 ? '0x' + b : '0x0' + b);
      }
      fs.writeSync(fd, '  ' + line.join(', ') + ',\n');
    }
    fs.writeSync(fd, '};\n');
    fs.closeSync(fd);
  },
}
