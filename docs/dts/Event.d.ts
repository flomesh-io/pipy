declare interface Event {
}

declare class Data extends Event {

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
