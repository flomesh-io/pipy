/// <reference path="./Message.d.ts" />

interface HttpMessageHead {
  protocol: string;
  headers: { [name: string]: string };
}

interface HttpMessageTail {
  headers: { [name: string]: string };
  headSize: number;
  bodySize: number;
}

interface HttpRequestHead extends HttpMessageHead {
  method: string;
  scheme: string;
  authority: string;
  path: string;
}

interface HttpResponseHead extends HttpMessageHead {
  status: number;
  statusText: string;
}

/**
 * Asynchronous HTTP client.
 */
interface HttpAgent {

  /**
   * Sends an HTTP request.
   *
   * @param method HTTP method.
   * @param path Request path.
   * @param headers HTTP headers.
   * @param body Request body.
   */
  request(
    method: string,
    path?: string,
    headers?: { [name: string]: string },
    body?: string | Data
  ): Promise<Message>;
}

interface HttpAgentConstructor {

  /**
   * Creates an instance of _http.Agent_.
   *
   * @param host IP Address or domain name of the HTTP server with optional port number.
   * @param options Options for TLS, TCP sockets and the HTTP muxer.
   * @returns An _http.Agent_ object.
   */
  new(
    host: string,
    options?: {
      tls?: {
        certificate?: CertificateOptions | (() => CertificateOptions),
        trusted?: Certificate[],
        verify?: (ok: boolean, cert: Certificate) => boolean,
        alpn?: string | string[],
        sni?: string | (() => string),
        handshake?: (protocolName: string | undefined) => void,
      },
      bind?: string | (() => string),
      congestionLimit?: number | string,
      bufferLimit?: number | string,
      retryCount?: number,
      retryDelay?: number | string,
      connectTimeout?: number | string,
      readTimeout?: number | string,
      writeTimeout?: number | string,
      idleTimeout?: number | string,
      keepAlive?: boolean,
      noDelay?: boolean,
      onState?: (inbound: Inbound) => void,
      maxIdle?: number | string,
      maxQueue?: number,
      maxMessages?: number,
      maxHeaderSize?: number | string,
      bufferSize?: number | string,
      version?: number | string | (() => number | string),
    },
  ): HttpAgent;
}

/**
 * Generates HTTP responses from a file directory.
 */
interface HttpDirectory {

  /**
   * Generates a response for a static file request.
   *
   * @param request A _Message_ object requesting a static file.
   * @returns A _Message_ object containing the HTTP response for the static file.
   */
  serve(request: Message): Message;
}

interface HttpDirectoryConstructor {

  /**
   * Creates an instance of _Directory_.
   *
   * @param path Path of the directory.
   * @returns An instance of _http.File_ created from the file.
   */
  new(
    path: string,
    options?: {
      fs?: boolean,
      tarball?: boolean,
      index?: string[],
      contentTypes?: (
        { [extension: string]: string } |
        ( (request: HttpRequestHead, pathname: string) => (string | { [extension: string]: string }) )
      ),
      defaultContentType?: string,
      compression?: (
        request: HttpRequestHead,
        acceptEncoding: { [algorithm: string]: true },
        pathname: string,
        size: number,
      ) => string,
    }
  ): HttpDirectory;

  /**
   * The default content type mapping.
   */
  defaultContentTypes: { [extension: string]: string };
}

interface Http {
  Agent: HttpAgentConstructor;
  Directory: HttpDirectoryConstructor;
}

declare var http: Http;
