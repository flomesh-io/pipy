/**
 * Creates a new configuration.
 *
 * @param {} contextPrototype
 */
declare function pipy(contextPrototype? : { [name: string]: any }) : Configuration;

declare namespace pipy {

  /**
   * Reads content of a file from the current codebase.
   */
  function load(filename: string): Data;

  /**
   * Evaluates expression from a file in the current codebase.
   */
  function solve(filename: string): any;

  /**
   * Reloads the current codebase.
   */
  function restart(): void;

  /**
   * Gracefully shuts down Pipy.
   */
  function exit(exitCode?: number = 0): void;

}
