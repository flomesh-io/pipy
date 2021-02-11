import { ID, PUBLIC_KEY } from './keys.js';

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
          digest = new crypto.Verify('sm2-sm3', {
            id: ID,
            key: PUBLIC_KEY,
          });
          break;

        case 'messageend':
          let result;
          if (digest.final(new Buffer(context.get('a.request.sig'), 'hex'))) {
            result = 'Verified\n';
          } else {
            result = 'Not verified\n';
          }
          output(new Event('messagestart'));
          output(new Buffer(result));
          output(new Event('messageend'));
          break;
      }
    }
  }
}
