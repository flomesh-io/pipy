import { KEY } from './keys.js';

export default (output, context) => {
  let buffer = null;

  return (input) => {
    if (input instanceof Buffer) {
      buffer.push(input);

    } else if (input instanceof Event) {
      switch (input.type) {
        case 'messagestart':
          buffer = new Buffer();
          break;

        case 'messageend':
          const cipher = new crypto.Cipher('sm4-cbc', KEY, new Buffer(16));
          const out = cipher.update(buffer);
          out.push(cipher.final());
          output(new Event('messagestart'));
          output(new Buffer(out.toString('base64') + '\n'));
          output(new Event('messageend'));
          break;
      }
    }
  }
}

