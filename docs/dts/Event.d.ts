/**
 * _Event_ base interface.
 */
interface Event {
}

/**
 * _Event_ to mark the start of a message in an event stream.
 */
interface MessageStart extends Event {

  /**
   * Protocol-dependent message header.
   */
  head?: object;
}

interface MessageStartConstructor {

  /**
   * Creates an instance of _MessageStart_.
   */
  new(head?: object): MessageStart;
}

/**
 * _Event_ to mark the end of a message in an event stream.
 */
interface MessageEnd extends Event {

  /**
   * Protocol-dependent message footer.
   */
  tail?: object;
}

interface MessageEndConstructor {

  /**
   * Creates an instance of _MessageEnd_.
   */
   new(tail?: object): MessageEnd;
}

/**
 * _Event_ to mark the end of an event stream.
 */
interface StreamEnd extends Event {

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
    | 'BufferOverflow';

}

interface StreamEndConstructor extends StreamEnd {

  /**
   * Creates an instance of _MessageEnd_.
   */
  new(error?: ''
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
    | 'BufferOverflow'
  ): StreamEnd;
}

/**
 * A chunk of bytes.
 */
interface Data extends Event {

  /**
   * Number of bytes.
   */
  size: number;

  /**
   * Appends bytes.
   */
  push(bytes: Data | string): Data;

  /**
   * Remove bytes from the beginning.
   */
  shift(count: number): Data;

  /**
   * Remove bytes from the beginning up to the first byte where user callback returns true.
   */
  shiftTo(callback: (byte: number) => boolean): Data;

  /**
   * Remove bytes from the beginning until user callback returns false.
   */
  shiftWhile(callback: (byte: number) => boolean): Data;

  /**
   * Converts to a string.
   */
  toString(encoding?: 'utf8' | 'hex' | 'base64' | 'base64url'): string;
}

interface DataConstructor {

  /**
   * Creates an instance of _Data_.
   */
   new(): Data;
   new(bytes: number[]): Data;
   new(text: string, encoding?: 'utf8' | 'hex' | 'base64' | 'base64url'): Data;
   new(data: Data): Data;

   from(text: string, encoding?: 'utf8' | 'hex' | 'base64' | 'base64url'): Data;
}

declare var MessageStart: MessageStartConstructor;
declare var MessageEnd: MessageEndConstructor;
declare var StreamEnd: StreamEndConstructor;
declare var Data: DataConstructor;
