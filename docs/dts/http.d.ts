declare namespace http {

  /**
   * Generates an HTTP response from a file in the current codebase.
   */
  class File {

    /**
     * Creates an instance of _File_.
     */
    static from(filename: string): File;

    /**
     * Converts to an HTTP response message.
     */
    toMessage(): Message;
  }

}
