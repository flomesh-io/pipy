/// <reference path="./Configuration.d.ts" />

/**
 * Creates a new configuration.
 *
 * @param contextPrototype An object containing key-value pairs of the context variable names and their initial values.
 * @returns A _Configuration_ object for setting up the pipeline layouts.
 */
declare function pipy(contextPrototype? : { [name: string]: any }) : Configuration;

declare namespace pipy {

  /**
   * Reads content of a file from the current codebase.
   *
   * @param filename A pathname in the current codebase.
   * @returns A _Data_ object containing the entire content of the specified file.
   */
  function load(filename: string): Data;

  /**
   * Evaluates expression from a file in the current codebase.
   *
   * @param filename A pathname in the current codebase.
   * @returns A value of any type after evaluating the expression in the specified file.
   */
  function solve(filename: string): any;

  /**
   * Reloads the current codebase.
   */
  function restart(): void;

  /**
   * Gracefully shuts down Pipy.
   *
   * @param exitCode A number for the process exit code.
   */
  function exit(exitCode?: number): void;
}
