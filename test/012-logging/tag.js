import { v1 as uuid } from './uuid.js';

export default (output, context) => (
  (input) => {
    if (input instanceof Event && input.type === 'messagestart') {
      context.set('uuid', uuid());
    }
    output(input);
  }
);
