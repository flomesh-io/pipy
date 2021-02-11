export default (output, context) => {
  let buf = null;

  return (input) => {
    if (input instanceof Event) {
      switch (input.type) {
        case 'sessionstart':
        case 'sessionend':
          output(input);
          break;

        case 'messagestart':
          buf = new Buffer();
          break;

        case 'messageend':
          const map = context.all('out.request.');

          output(new Event('messagestart'));
          output(new Buffer(JSON.stringify({
            uuid: context.get('uuid'),
            timestamp: Date.now() * 1000 + new Date().getMilliseconds(),
            type: 'response',
            body: buf.toString(),
            ctx: context.all(),
          })));
          output(new Event('messageend'));
          break;
      }
    } else if (input instanceof Buffer) {
      buf.push(input);
    }
  }
}
