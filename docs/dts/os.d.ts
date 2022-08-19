/// <reference no-default-lib="true"/>

interface Stats {
  readonly dev: number;
  readonly ino: number;
  readonly mode: number;
  readonly nlink: number;
  readonly uid: number;
  readonly gid: number;
  readonly rdev: number;
  readonly size: number;
  readonly blksize: number;
  readonly blocks: number;
  readonly atime: number;
  readonly mtime: number;
  readonly ctime: number;

  isFile(): boolean;
  isDirectory(): boolean;
  isCharacterDevice(): boolean;
  isBlockDevice(): boolean;
  isFIFO(): boolean;
  isSymbolicLink(): boolean;
  isSocket(): boolean;
}

interface OS {

  /**
   * Environment variables.
   */
  env: { [name: string]: string };

  /**
   * Obtains information about a file.
   */
  stat(filename: string): Stats;

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
