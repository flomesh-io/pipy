interface Console {

  /**
   * Log values to the terminal.
   */
  log(...values: any[]): void;
}

declare var console: Console;
