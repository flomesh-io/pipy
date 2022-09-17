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
   *
   * @param head An object containing protocol-dependent message header.
   * @returns A _MessageStart_ object with the specified message header.
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
   *
   * @param tail An object containing protocol-dependent message footer.
   * @returns A _MessageEnd_ object with the specified message footer.
   */
   new(tail?: object): MessageEnd;
}

/**
 * _Event_ to mark the end of an event stream.
 */
interface StreamEnd extends Event {

  /**
   * Error type if any. Possible values include:
   *   - `""` (no error)
   *   - `"Replay"` (used with _replay_ filter)
   *   - `"UnknownError"`
   *   - `"RuntimeError"`
   *   - `"ReadError"`
   *   - `"WriteError"`
   *   - `"CannotResolve"`
   *   - `"ConnectionCanceled"`
   *   - `"ConnectionReset"`
   *   - `"ConnectionRefused"`
   *   - `"ConnectionTimeout"`
   *   - `"ReadTimeout"`
   *   - `"WriteTimeout"`
   *   - `"IdleTimeout"`
   *   - `"Unauthorized"`
   *   - `"BufferOverflow"`
   */
  error: ''
    | 'Replay'
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
   *
   * @param error Error type. Available error types include:
   *   - `""` (default)
   *   - `"Replay"` (used with _replay_ filter)
   *   - `"UnknownError"`
   *   - `"RuntimeError"`
   *   - `"ReadError"`
   *   - `"WriteError"`
   *   - `"CannotResolve"`
   *   - `"ConnectionCanceled"`
   *   - `"ConnectionReset"`
   *   - `"ConnectionRefused"`
   *   - `"ConnectionTimeout"`
   *   - `"ReadTimeout"`
   *   - `"WriteTimeout"`
   *   - `"IdleTimeout"`
   *   - `"Unauthorized"`
   *   - `"BufferOverflow"`
   * @returns A _StreamEnd_ object with the specified error if any.
   */
  new(error?: ''
    | 'Replay'
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
   *
   * @param bytes A _Data_ object containing the bytes or a string containing UTF-8 bytes.
   * @returns The same _Data_ object after bytes are appended.
   */
  push(bytes: Data | string): Data;

  /**
   * Removes bytes from the beginning.
   *
   * @param count Number of bytes to remove from the beginning.
   * @returns A _Data_ object containing bytes removed from the beginning.
   */
  shift(count: number): Data;

  /**
   * Removes bytes from the beginning up to the first byte where user callback returns true.
   *
   * @param callback A function that receives each byte being removed and returns `true` for the last byte to remove.
   * @returns A _Data_ object containing bytes removed from the beginning.
   */
  shiftTo(callback: (byte: number) => boolean): Data;

  /**
   * Removes bytes from the beginning until user callback returns false.
   *
   * @param callback A function that receives each byte being removed and returns `false` for the first byte to keep.
   * @returns A _Data_ object containing bytes removed from the beginning.
   */
  shiftWhile(callback: (byte: number) => boolean): Data;

  /**
   * Converts to a string.
   *
   * @param encoding Interpretation of the characters in the converted string. Valid options include:
   *   - `"utf8"` (default) - As a UTF-8 encoded string.
   *   - `"hex"` - As hexadecimal numbers.
   *   - `"base64"` - [Base64](https://datatracker.ietf.org/doc/html/rfc4648#section-4) representation.
   *   - `"base64url"` - [Base64url](https://datatracker.ietf.org/doc/html/rfc4648#section-5) representation.
   * @returns The converted string.
   */
  toString(encoding?: 'utf8' | 'hex' | 'base64' | 'base64url'): string;
}

interface DataConstructor {

  /**
   * Creates an instance of _Data_.
   *
   * @returns An empty _Data_ object.
   */
  new(): Data;

  /**
   * Creates an instance of _Data_ from an array of bytes.
   *
   * @param bytes - An array of numbers representing the bytes.
   * @returns A _Data_ object containing the bytes from the array.
   */
  new(bytes: number[]): Data;

  /**
   * Creates an instance of _Data_ from a string.
   *
   * @param text A string to encode or decode into bytes.
   * @param encoding Interpretation of the characters in the string. Valid options include:
   *   - `"utf8"` (default) - As a UTF-8 encoded string.
   *   - `"hex"` - As hexadecimal numbers.
   *   - `"base64"` - [Base64](https://datatracker.ietf.org/doc/html/rfc4648#section-4) representation.
   *   - `"base64url"` - [Base64url](https://datatracker.ietf.org/doc/html/rfc4648#section-5) representation.
   * @returns A _Data_ object converted from the string.
   */
  new(text: string, encoding?: 'utf8' | 'hex' | 'base64' | 'base64url'): Data;

  /**
   * Creates an instance of _Data_ by copying bytes from another _Data_ object.
   *
   * @param data A _Data_ object to copy bytes from.
   * @returns A _Data_ object copied from the other _Data_ object.
   */
  new(data: Data): Data;

  /**
   * Converts a string to an instance of _Data_.
   *
   * @param text - A string to convert to _Data_.
   * @param encoding Interpretation of the characters in the string. Valid options include:
   *   - `"utf8"` (default) - As a UTF-8 encoded string.
   *   - `"hex"` - As hexadecimal numbers.
   *   - `"base64"` - [Base64](https://datatracker.ietf.org/doc/html/rfc4648#section-4) representation.
   *   - `"base64url"` - [Base64url](https://datatracker.ietf.org/doc/html/rfc4648#section-5) representation.
   * @returns Instance of _Data_ converted from the string.
   */
  from(text: string, encoding?: 'utf8' | 'hex' | 'base64' | 'base64url'): Data;
}

declare var MessageStart: MessageStartConstructor;
declare var MessageEnd: MessageEndConstructor;
declare var StreamEnd: StreamEndConstructor;
declare var Data: DataConstructor;
