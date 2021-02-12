import { KEY } from './keys.js';

export default (output, context) => {
  let buffer = null;

  return (input) => {
    if (input instanceof Buffer) {
      buffer.push(input);

    } else if (input instanceof Event) {
      switch (input.type) {
        case 'sessionstart':
        case 'sessionend':
          output(input);
          break;

        case 'messagestart':
          buffer = new Buffer();
          break;

        case 'messageend':
          const decipher = new crypto.Decipher('sm4-cbc', KEY, new Buffer(16));
          const out = decipher.update(new Buffer(buffer.toString(), 'base64'));
          out.push(decipher.final());
          output(new Event('messagestart'));
          output(out);
          output(new Event('messageend'));
          break;
      }
    }
  }
}

