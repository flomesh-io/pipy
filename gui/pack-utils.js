const fs = require('fs');

module.exports = {
  formatSize: size => {
    const s = Math.round(size / 1024).toString();
    return ' '.repeat(6 - s.length) + s + 'K';
  },

  writeBinaryHeaderFile: (filename, name, data) => {
    const fd = fs.openSync(filename, 'w+');
    fs.writeSync(fd, `static unsigned char ${name}[${data.length}] = {\n`);
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
