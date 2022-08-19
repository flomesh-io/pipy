/// <reference no-default-lib="true"/>

/**
 * Logger base interface.
 */
interface Logger {

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
          key: PrivateKey;
          cert: Certificate | CertificateChain;
        },
        trusted?: Certificate[],
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
interface BinaryLogger extends Logger {
}

interface BinaryLoggerConstructor {

  /**
   * Creates an instance of _BinaryLogger_.
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
   */
  new(name: string): JSONLogger;
}

interface Logging {
  BinaryLogger: BinaryLoggerConstructor,
  TextLogger: TextLoggerConstructor,
  JSONLogger: JSONLoggerConstructor,
}

declare var logging: Logging;
