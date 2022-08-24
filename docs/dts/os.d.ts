interface OS {

  /**
   * Environment variables.
   */
  env: { [name: string]: string };

  /**
   * Read the entire content of a file.
   */
  readFile(filename: string): Data;

  /**
   * Write the entire content of a file.
   */
  writeFile(filename: string, content: Data | string): void;
}

declare var os: OS;
