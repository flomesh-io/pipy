declare class URL {

  /**
   * Creates an instance of _URL_.
   */
  constructor(url: string, base?: string);

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
