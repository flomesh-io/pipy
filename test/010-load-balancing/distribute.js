import TARGETS from './targets.js';
const N = TARGETS.length;

let i = 0; // Current index of target

export default (output, context) => (
  input => {

    // Rotate targets for each session
    if (input instanceof Event &&
        input.type === 'sessionstart'
    ) {
      i = (i + 1) % N;
      context.set('target', TARGETS[i]);
    }

    // Pass down everything
    output(input);
  }
);
