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
   */
  new(url: string, base?: string): URL;
}

interface URLSearchParams {

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

interface URLSearchParamsConstructor {

  /**
   * Creates an instance of _URLSearchParams_.
   */
  new(search: string): URLSearchParams;
}

declare var URL: URLConstructor;
declare var URLSearchParams: URLSearchParamsConstructor;
