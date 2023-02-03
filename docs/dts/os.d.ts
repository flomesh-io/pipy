interface OS {

  /**
   * Environment variables.
   */
  env: { [name: string]: string };

  /**
   * List filenames in a directory.
   *
   * @param filename Pathname of the directory to read.
   * @returns An array of filenames contained in that directory.
   */
  readDir(filename: string): string[];

  /**
   * Read the entire content of a file.
   *
   * @param filename Pathname of the file to read.
   * @returns A _Data_ object containing the entire content of the file.
   */
  readFile(filename: string): Data;

  /**
   * Write the entire content of a file.
   *
   * @param filename Pathname of the file to write.
   * @param content A string or a _Data_ object containing the entire content of the file.
   */
  writeFile(filename: string, content: Data | string): void;
}

declare var os: OS;
