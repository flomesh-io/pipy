declare class URLSearchParams {

  /**
   * Creates an instance of _URLSearchParams_.
   */
  constructor(search: string);

  /**
   * Retreives all values with a name.
   */
  getAll(name: string): string[];

  /**
   * Gets the value of a name.
   */
  get(name: string): string | null;

  /**
   * Sets the value or values of a name.
   */
  set(name: string, value: string | string[]);

  /**
   * Makes an object with the key-value pairs.
   */
  toObject(): { [name: string]: string | string[] };

}
