/**
 * Event to mark the end of an event stream.
 */
declare class StreamEnd extends Event {

  /**
   * Creates an instance of _MessageEnd_ object.
   */
  constructor(error?: ''
    | 'UnknownError'
    | 'RuntimeError'
    | 'ReadError'
    | 'WriteError'
    | 'CannotResolve'
    | 'ConnectionCanceled'
    | 'ConnectionReset'
    | 'ConnectionRefused'
    | 'ConnectionTimeout'
    | 'ReadTimeout'
    | 'WriteTimeout'
    | 'IdleTimeout'
    | 'Unauthorized'
    | 'BufferOverflow' = ''
  );

  /**
   * Error type if any.
   */
  error: ''
    | 'UnknownError'
    | 'RuntimeError'
    | 'ReadError'
    | 'WriteError'
    | 'CannotResolve'
    | 'ConnectionCanceled'
    | 'ConnectionReset'
    | 'ConnectionRefused'
    | 'ConnectionTimeout'
    | 'ReadTimeout'
    | 'WriteTimeout'
    | 'IdleTimeout'
    | 'Unauthorized'
    | 'BufferOverflow' = '';
}
