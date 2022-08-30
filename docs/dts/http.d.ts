/// <reference path="./Message.d.ts" />

/**
 * Generates an HTTP response from a file in the current codebase.
 */
interface HttpFile {

  /**
   * Converts to an HTTP response message.
   *
   * @returns A _Message_ object containing an HTTP response for the static file.
   */
  toMessage(): Message;
}

interface HttpFileConstructor {

  /**
   * Creates an instance of _File_.
   *
   * @param filename Pathname of a file.
   * @returns An instance of _http.File_ created from the file.
   */
  from(filename: string): HttpFile;
}

interface Http {
  File: HttpFileConstructor;
}

declare var http: Http;
