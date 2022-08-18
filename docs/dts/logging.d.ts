declare namespace logging {

  /**
   * Logger base interface.
   */
  abstract class Logger {

    /**
     * Adds output to the standard output.
     */
    toStdout(): Logger;

    /**
     * Adds output to the standard error.
     */
    toStderr(): Logger;

    /**
     * Adds output to a file.
     */
    toFile(filename: string): Logger;

    /**
     * Adds output to an HTTP endpoint.
     */
    toHTTP(
      url: string,
      options?: {
        method?: string,
        headers?: { [name: string]: string },
        batch?: {
          size?: number,
          interval?: number | string,
          timeout?: number | string,
          vacancy?: number,
        },
        tls: {
          certificate?: {
            key: crypto.PrivateKey,
            cert: crypto.Certificate | crypto.CertificateChain,
          } | (
            () => {
              key: crypto.PrivateKey,
              cert: crypto.Certificate | crypto.CertificateChain,
            }
          ),
          trusted?: crypto.Certificate[],
        },
      }
    ): Logger;

    /**
     * Writes to the log.
     */
    log(...values: any[]): void;

  }

  /**
   * Log values as binary data.
   */
  class BinaryLogger extends Logger {

    /**
     * Creates an instance of _BinaryLogger_.
     */
    constructor(name: string);

  }

  /**
   * Log values as text.
   */
  class TextLogger extends Logger {

    /**
     * Creates an instance of _TextLogger_.
     */
    constructor(name: string);

  }

  /**
   * Log values in JSON format.
   */
  class JSONLogger extends Logger {

    /**
     * Creates an instance of _JSONLogger_.
     */
    constructor(name: string);

  }

}
