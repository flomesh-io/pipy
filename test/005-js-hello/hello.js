//
// Handles a new session, returns a stream handler for it.
//

export default (output, context) => (

  //
  // Stream handler
  //

  input => {
    if (input instanceof Event) {
      switch (input.type) {
        case 'sessionstart':
        case 'sessionend':
          output(input);
          break;

        // On reception of a message end
        case 'messageend': {

          // Set 'Content-Type' header
          context.set('a.response.Content-Type', 'text/plain');

          // Write a hello message down the pipeline
          output(new Event('messagestart'));
          output(new Buffer('Hello!\n'));
          output(new Event('messageend'));
          break;
        }
      }
    }
  }
);
