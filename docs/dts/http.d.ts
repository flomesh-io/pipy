/// <reference path="./Message.d.ts" />

/**
 * Generates an HTTP response from a file in the current codebase.
 */
interface HttpFile {

  /**
   * Converts to an HTTP response message.
   */
  toMessage(): Message;
}

interface HttpFileConstructor {

  /**
   * Creates an instance of _File_.
   */
  from(filename: string): HttpFile;
}

interface Http {
  File: HttpFileConstructor;
}

declare var http: Http;
