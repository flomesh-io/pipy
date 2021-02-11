//
// A script module is expected to export one single function, which is
// called on creation of every new session, returning a stream handler
// for processing every object running through in that session.
//

// Called once for every new session
export default (output) => (

  // Called once for every object running through on the stream
  (input) => output(input)

);

