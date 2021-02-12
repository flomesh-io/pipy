//
// Handles a new session, returns a stream handler for it.
//

export default (output, context) => (
  buildJsonTree(output, obj => {
    output(new Event('messagestart'));
    output(new Buffer(
      JSON.stringify(obj, null, 2) + '\n'
    ));
    output(new Event('messageend'));
  })
);

//
// Returns a stream handler that puts JSON values together as
// a plain JavaScript object.
//

const buildJsonTree = (output, cb) => {
  let root, stack, lastKey;

  const append = (v) => {
    const n = stack.length;
    if (n === 0) {
      root = v;
    } else {
      const p = stack[n-1];
      if (p instanceof Array) p.push(v);
      else if (lastKey) p[lastKey] = v;
    }
  }

  const push = (v) => {
    append(v);
    stack.push(v);
  }

  return input => {
    if (input instanceof Event) {
      switch (input.type) {
        case 'sessionstart':
        case 'sessionend': output(input); break;
        case 'messagestart': stack = []; break;
        case 'messageend': cb(root); break;
        case 'mapstart': push({}); break;
        case 'mapkey': lastKey = input.value; break;
        case 'mapend': stack.pop(); break;
        case 'liststart': push([]); break;
        case 'listend': stack.pop(); break;
      }
    } else if (input instanceof Buffer) {
      // do nothing
    } else {
      append(input);
    }
  }
}
