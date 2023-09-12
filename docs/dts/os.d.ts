interface Stats {
  dev: number;
  ino: number;
  mode: number;
  nlink: number;
  uid: number;
  gid: number;
  rdev: number;
  size: number;
  blksize: number;
  blocks: number;
  atime: number;
  mtime: number;
  ctime: number;

  isFile(): boolean;
  isDirectory(): boolean;
  isCharacterDevice(): boolean;
  isBlockDevice(): boolean;
  isFIFO(): boolean;
  isSymbolicLink: boolean;
  isSocket(): boolean;
}

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

  /**
   * Retrieves information about a file.
   *
   * @param filename Pathname of the file to retrieve information about.
   * @return Information about the file.
   */
  stat(filename: string): Stats;

  /**
   * Deletes a file.
   *
   * @param filename Pathname of the file to delete.
   * @return Whether or not the deletion was successful.
   */
  unlink(filename: string): boolean;
}

declare var os: OS;
