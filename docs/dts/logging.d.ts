/**
 * Logger base interface.
 */
interface Logger {

  /**
   * Adds output to the standard output.
   *
   * @returns The same logger object.
   */
  toStdout(): Logger;

  /**
   * Adds output to the standard error.
   *
   * @returns The same logger object.
   */
  toStderr(): Logger;

  /**
   * Adds output to a file.
   *
   * @param filename Pathname of the file to write to.
   * @returns The same logger object.
   */
  toFile(filename: string): Logger;

  /**
   * Adds output to the [Syslog](https://en.wikipedia.org/wiki/Syslog).
   *
   * @param priority Severity of the log messages.
   *   Can be one of:
   *   - `"EMERG"`
   *   - `"ALERT"`
   *   - `"CRIT"`
   *   - `"ERR"`
   *   - `"WARNING"`
   *   - `"NOTICE"`
   *   - `"INFO"` (Default)
   *   - `"DEBUG"`
   * @returns The same logger object.
   */
  toSyslog(
    priority?:
      "EMERG"
    | "ALERT"
    | "CRIT"
    | "ERR"
    | "WARNING"
    | "NOTICE"
    | "INFO"
    | "DEBUG"
  ): Logger;

  /**
   * Adds output to an HTTP endpoint.
   *
   * @param url URL to send log to.
   * @param options Options including:
   *   - _method_ - HTTP request method. Default is `"POST"`.
   *   - _headers_ - An object of key-value pairs for HTTP header items.
   *   - _batch_ - Batching settings.
   *   - _batch.size_ - Number of log items in each batch.
   *   - _batch.vacancy_ - Percentage of spare space letf in the internal storage of packed _Data_ object. Default is `0.5`.
   *   - _batch.interval_ - Maximum time to wait before outputting a batch even if the number of messages is not enough.
   *       Can be a number in seconds or a string with one of the time unit suffixes such as `s`, `m` or `h`.
   *       Default is _5 seconds_.
   *   - _tls_ - Optional TLS settings if using HTTPS.
   *   - _tls.certificate_ - An object containing _cert_ and _key_,
   *       where _cert_ can be a _crypto.Certificate_ or a _crypto.CertificateChain_
   *       and _key_ must be a _crypto.PrivateKey_.
   *   - _tls.trusted_ - An array of _crypto.Certificate_ objects for allowed client certificates.
   * @returns The same logger object.
   */
  toHTTP(
    url: string,
    options?: {
      method?: string,
      headers?: { [name: string]: string },
      bufferLimit?: number,
      batch?: {
        size?: number,
        vacancy?: number,
        timeout?: number | string,
        interval?: number | string,
        prefix?: string,
        postfix?: string,
        separator?: string,
      },
      tls: {
        certificate?: {
          key: PrivateKey;
          cert: Certificate | CertificateChain;
        },
        trusted?: Certificate[],
      },
    }
  ): Logger;

  /**
   * Writes to the log.
   *
   * @param values Values to log.
   */
  log(...values: any[]): void;

}

/**
 * Log values as binary data.
 */
interface BinaryLogger extends Logger {
}

interface BinaryLoggerConstructor {

  /**
   * Creates an instance of _BinaryLogger_.
   *
   * @param name Name of the logger.
   * @returns A _BinaryLogger_ object with the specified name.
   */
  new(name: string): BinaryLogger;
}

/**
 * Log values as text.
 */
interface TextLogger extends Logger {
}

interface TextLoggerConstructor {

  /**
   * Creates an instance of _TextLogger_.
   *
   * @param name Name of the logger.
   * @returns A _TextLogger_ object with the specified name.
   */
  new(name: string): TextLogger;
}

/**
 * Log values in JSON format.
 */
interface JSONLogger extends Logger {
}

interface JSONLoggerConstructor {

  /**
   * Creates an instance of _JSONLogger_.
   *
   * @param name Name of the logger.
   * @returns A _JSONLogger_ object with the specified name.
   */
  new(name: string): JSONLogger;
}

interface Logging {
  BinaryLogger: BinaryLoggerConstructor,
  TextLogger: TextLoggerConstructor,
  JSONLogger: JSONLoggerConstructor,
}

declare var logging: Logging;
