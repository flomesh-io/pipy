//
// Handles a new session, returns a stream handler for it.
//

export default (output, context) => (
  readBody(output, buffer => {
    let obj = null;
    try {
      obj = JSON.parse(buffer.toString());
    } catch (e) {}
    output(new Event('messagestart'));
    output(new Buffer(JSON.stringify(obj, null, 2) + '\n'));
    output(new Event('messageend'));
  })
);

//
// Creates a stream handler that collects all data chunks
// of a complete message and returns it as a whole buffer.
//

const readBody = (output, cb) => {
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
          cb(buffer);
          break;
      }
    }
  }
}
