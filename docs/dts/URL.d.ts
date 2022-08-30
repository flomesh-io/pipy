/**
 * A uniform resource locator conforming to [RFC 3986](https://datatracker.ietf.org/doc/html/rfc3986).
 */
interface URL {

  /**
   * Username and password.
   */
  readonly auth: string;

  /**
   * Fragment identifier.
   */
  readonly hash: string;

  /**
   * Host and port.
   */
  readonly host: string;

  /**
   * Host without the port.
   */
  readonly hostname: string;

  /**
   * Full URL.
   */
  readonly href: string;

  /**
   * Origin (host and protocol).
   */
  readonly origin: string;

  /**
   * Password.
   */
  readonly password: string;

  /**
   * Path with query string.
   */
  readonly path: string;

  /**
   * Path without the query string.
   */
  readonly pathname: string;

  /**
   * Port number.
   */
  readonly port: string;

  /**
   * Protocol.
   */
  readonly protocol: string;

  /**
   * Query string.
   */
  readonly query: string;

  /**
   * Query string with the leading question mark.
   */
  readonly search: string;

  /**
   * Key-value pairs in the query string.
   */
  readonly searchParams: URLSearchParams;

  /**
   * Username.
   */
  readonly username: string;
}

interface URLConstructor {

  /**
   * Creates an instance of _URL_.
   *
   * @param url A string containing the full or partial URL.
   * @param base An optional string containing the base of the URL.
   * @returns A _URL_ object representing the given URL.
   */
  new(url: string, base?: string): URL;
}

/**
 * A utility for parsing and formatting the query string in a URL.
 */
interface URLSearchParams {

  /**
   * Retreives all values with a name.
   *
   * @param name The name of the values to query.
   * @returns An array of string values under the queried name.
   */
  getAll(name: string): string[];

  /**
   * Gets the value of a name.
   *
   * @param name The name of the value to query.
   * @returns A string containing the first value under the queried name,
   *   or `null` if the specified name isn't found.
   */
  get(name: string): string | null;

  /**
   * Sets the value or values of a name.
   *
   * @param name The name of the value to update.
   * @param value A value or an array of values to update for the specified name.
   */
  set(name: string, value: string | string[]);

  /**
   * Makes an object with the key-value pairs
   *
   * @returns An object containing key-value pairs of all items in the query string.
   */
  toObject(): { [name: string]: string | string[] };

  /**
   * Composes the query string.
   *
   * @returns A string formatted from all the items using `'&'` and `'='` separators.
   */
  toString(): string;
}

interface URLSearchParamsConstructor {

  /**
   * Creates an instance of _URLSearchParams_.
   *
   * @param search A string containing the query string, with or without a leading `'?'`.
   * @returns A _URLSearchParams_ object representing the search string.
   */
  new(search: string): URLSearchParams;
}

declare var URL: URLConstructor;
declare var URLSearchParams: URLSearchParamsConstructor;
