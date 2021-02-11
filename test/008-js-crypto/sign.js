import { ID, PRIVATE_KEY } from './keys.js';

export default (output, context) => {
  let digest = null;

  return (input) => {
    if (input instanceof Buffer) {
      digest.update(input);

    } else if (input instanceof Event) {
      switch (input.type) {
        case 'sessionstart':
        case 'sessionend':
          output(input);
          break;

        case 'messagestart':
          digest = new crypto.Sign('sm2-sm3', {
            id: ID,
            key: PRIVATE_KEY,
          });
          break;

        case 'messageend':
          const result = digest.final().toString('hex') + '\n';
          output(new Event('messagestart'));
          output(new Buffer(result));
          output(new Event('messageend'));
          break;
      }
    }
  }
}
