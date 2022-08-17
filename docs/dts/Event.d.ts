/**
 * _Event_ base interface.
 */
abstract class Event {
}

/**
 * _Event_ to mark the start of a message in an event stream.
 */
declare class MessageStart implements Event {

  /**
   * Creates an instance of _MessageStart_.
   */
  constructor(head?: object);

  /**
   * Protocol-dependent message header.
   */
  head?: object;
}

/**
 * _Event_ to mark the end of a message in an event stream.
 */
declare class MessageEnd implements Event {

  /**
   * Creates an instance of _MessageEnd_.
   */
  constructor(tail?: object);

  /**
   * Protocol-dependent message footer.
   */
  tail?: object;

}

/**
 * _Event_ to mark the end of an event stream.
 */
declare class StreamEnd implements Event {

  /**
   * Creates an instance of _MessageEnd_.
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

/**
 * A chunk of bytes.
 */
declare class Data implements Event {

  static from(text: string, encoding?: 'utf8' | 'hex' | 'base64' | 'base64url' = 'utf8'): Data;

  /**
   * Creates an instance of _Data_.
   */
  constructor();
  constructor(bytes: number[]);
  constructor(text: string, encoding?: 'utf8' | 'hex' | 'base64' | 'base64url' = 'utf8');
  constructor(data: Data);

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
  toString(encoding?: 'utf8' | 'hex' | 'base64' | 'base64url' = 'utf8'): string;

}
