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
   * Removes bytes from the beginning.
   */
  shift(count: number): Data;

  /**
   * Removes bytes from the beginning up to the first byte where user callback returns true.
   */
  shiftTo(callback: (byte: number) => boolean): Data;

  /**
   * Removes bytes from the beginning until user callback returns false.
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

  /**
   * Creates an instance of _Data_ from an array of bytes.
   * @param bytes - An array of numbers representing the bytes.
   */
  new(bytes: number[]): Data;
  new(text: string, encoding?: 'utf8' | 'hex' | 'base64' | 'base64url'): Data;
  new(data: Data): Data;

  /**
   * Converts a string to an instance of _Data_.
   *
   * @param text - A string to convert to _Data_.
   * @param encoding - Interpretation of the characters. The following are supported:
   *   - _"utf8"_: (default) Encode the text as UTF-8
   *   - _"hex"_: Decode the text as hexadecimal representation
   *   - _"base64"_: Decode the text as Base64 format
   *   - _"base64url"_: Decode the text as Base64URL format
   * @returns Instance of _Data_ converted from the string.
   */
  from(text: string, encoding?: 'utf8' | 'hex' | 'base64' | 'base64url'): Data;
}

declare var MessageStart: MessageStartConstructor;
declare var MessageEnd: MessageEndConstructor;
declare var StreamEnd: StreamEndConstructor;
declare var Data: DataConstructor;
