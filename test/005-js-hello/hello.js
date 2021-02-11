//
// Handles a new session, returns a stream handler for it.
//

export default (output, context) => (

  //
  // Stream handler
  //

  input => {

    // On reception of a message end
    if (input instanceof Event && input.type === 'messageend') {

      // Set 'Content-Type' header
      context.set('a.response.Content-Type', 'text/plain');

      // Write a hello message down the pipeline
      output(new Event('messagestart'));
      output(new Buffer('Hello!\n'));
      output(new Event('messageend'));
    }
  }
);
