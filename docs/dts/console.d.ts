interface Console {

  /**
   * Log values to the terminal.
   *
   * @param values Values to log to the terminal.
   */
  log(...values: any[]): void;

  /**
   * Log values to the terminal.
   *
   * @param values Values to log to the terminal.
   */
  info(...values: any[]): void;

  /**
   * Log values to the terminal.
   *
   * @param values Values to log to the terminal.
   */
  debug(...values: any[]): void;

  /**
   * Log values to the terminal.
   *
   * @param values Values to log to the terminal.
   */
  warn(...values: any[]): void;

  /**
   * Log values to the terminal.
   *
   * @param values Values to log to the terminal.
   */
  error(...values: any[]): void;
}

declare var console: Console;
