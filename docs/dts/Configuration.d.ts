/**
 * Configuration is used to set up context variables and pipeline layouts in a module.
 */
declare class Configuration {

  /**
   * Defines context variables that are accessible to other modules.
   */
  export(namespace: string, variables: { [key: string]: any }): Configuration;

  /**
   * Imports context variables defined and exported from other modules.
   */
  import(variables: { [key: string]: string }): Configuration;

  /**
   * Creates a pipeline layout for incoming TCP/UDP connections on a port.
   */
  listen(port: number | null, options?: {}): Configuration;

  /**
   * Creates a pipeline layout for reading from a file.
   */
  read(filename: string): Configuration;

  /**
   * Creates a pipeline layout for a periodic job or a signal. 
   */
  task(intervalOrSignal?: number | string): Configuration;

  /**
   * Creates a sub-pipeline layout.
   */
  pipeline(name?: string): Configuration;

  /**
   * Registers a function to be called when a pipeline is created.
   */
  onStart(handler: () => Event | Message | (Event|Message)[]): Configuration;

  /**
   * Registers a function to be called when a pipeline is destroyed.
   */
  onEnd(handler: () => void): Configuration;

  /**
   * Attaches a sub-pipeline layout to the last joint filter.
   */
  to(pipelineLayout: string | ((pipelineConfigurator: Configuration) => void)): Configuration;

  /**
   * Appends an _acceptHTTPTunnel_ filter to the current pipeline layout.
   *
   * An _acceptHTTPTunnel_ filter implements HTTP tunnel on the server side.
   *
   * - **INPUT** - _Data_ stream received from the client with a leading HTTP CONNECT request _Message_.
   * - **OUTPUT** - _Data_ stream to send to the client with a leading HTTP CONNECT response _Message_.
   * - **SUB-INPUT** - _Data_ stream received from the client via HTTP tunnel.
   * - **SUB-OUTPUT** - _Data_ stream to send to the client via HTTP tunnel.
   */
  acceptHTTPTunnel(handler: (request: Message) => Message): Configuration;

  /**
   * Appends an _acceptSOCKS_ filter to the current pipeline layout.
   *
   * An _acceptSOCKS_ filter implements SOCKS protocol on the server side.
   *
   * - **INPUT** - _Data_ stream received from the client with a leading SOCKS connection request.
   * - **OUTPUT** - _Data_ stream to send to the client with a leading SOCKS connection response.
   * - **SUB-INPUT** - _Data_ stream received from the client via SOCKS.
   * - **SUB-OUTPUT** - _Data_ stream to send to the client via SOCKS.
   */
  acceptSOCKS(handler: (host, port, id) => boolean): Configuration;

  /**
   * Appends an _acceptTLS_ filter to the current pipeline layout.
   *
   * An _acceptTLS_ filter implements TLS protocol on the server side.
   *
   * - **INPUT** - TLS-encrypted _Data_ stream received from the client.
   * - **OUTPUT** - TLS-encrypted _Data_ stream to send to the client.
   * - **SUB-INPUT** - _Data_ stream received from the client after TLS decryption.
   * - **SUB-OUTPUT** - _Data_ stream to send to the client before TLS encryption.
   */
  acceptTLS(
    options?: {
      certificate?: {
        key: crypto.PrivateKey,
        cert: crypto.Certificate | crypto.CertificateChain,
      } | (
        (sni: string) => {
          key: crypto.PrivateKey,
          cert: crypto.Certificate | crypto.CertificateChain,
        }
      ),
      trusted?: crypto.Certificate[],
      alpn?: string[] | ((protocolNames: string[]) => number),
      handshake?: (protocolName: string | undefined) => void,
    }
  ): Configuration;

  /**
   * Appends a _branch_ filter to the current pipeline layout.
   *
   * A _branch_ filter selects a pipeline layout from a number of candidates based on condition callbacks,
   * and then creates a sub-pipeline from the selected pipeline layout before streaming events through it.
   *
   * - **INPUT** - Any types of _Events_.
   * - **OUTPUT** - _Events_ streaming out from the selected sub-pipeline.
   * - **SUB-INPUT** - _Events_ streaming into the _branch_ filter.
   * - **SUB-OUTPUT** - Any types of _Events_.
   */
  branch(
    condition: () => boolean,
    pipelineLayout: string|((pipelineConfigurator: Configuration) => void),
    ...restBranches: ((() => boolean)|string|((pipelineConfigurator: Configuration) => void))[]
  ): Configuration;

  /**
   * Appends a _chain_ filter to the current pipeline layout.
   *
   * When given a list of module filenames,
   * a _chain_ filter starts a module chain and links to the entry pipeline for the first module.
   *
   * When no arguments are present,
   * a _chain_ filter links to the entry pipeline for the next module on the current module chain.
   *
   * - **INPUT** - Any types of _Events_.
   * - **OUTPUT** - _Events_ streaming out from the selected sub-pipeline.
   * - **SUB-INPUT** - _Events_ streaming into the _chain_ filter.
   * - **SUB-OUTPUT** - Any types of _Events_.
   */
  chain(modules?: string[]): Configuration;

  /**
   * Appends a _compressHTTP_ filter to the current pipeline layout.
   *
   * A _compressHTTP_ filter compresses HTTP messages.
   *
   * - **INPUT** - HTTP _Messages_ to compress.
   * - **OUTPUT** - Compressed HTTP _Messages_.
   */
  compressHTTP(
    { method = '', level = 'default' }?: {
      method?: '' | 'deflate' | 'gzip' | (() => (''|'deflate'|'gzip')),
      level?: 'default' | 'speed' | 'best' | (() => ('default'|'speed'|'best')),
    }
  ): Configuration;

  /**
   * Appends a _compressMessage_ filter to the current pipeline layout.
   *
   * A _compressMessage_ filter compresses messages.
   *
   * - **INPUT** - _Messages_ to compress.
   * - **OUTPUT** - Compressed _Messages_.
   */
  compressMessage(
    { method = '', level = 'default' }?: {
      method?: '' | 'deflate' | 'gzip' | (() => (''|'deflate'|'gzip')),
      level?: 'default' | 'speed' | 'best' | (() => ('default'|'speed'|'best')),
    }
  ): Configuration;

  /**
   * Appends a _connect_ filter to the current pipeline layout.
   *
   * A _connect_ filter establishes a TCP connection to a remote host.
   *
   * - **INPUT** - _Data_ stream to send to the host.
   * - **OUTPUT** - _Data_ stream received from the host.
   */
  connect(
    target: string | (() => string),
    { bufferLimit = 0, retryCount = 0, retryDelay = 0,
      connectTimeout = 0, readTimeout = 0, writeTimeout = 0, idleTimeout = 0,
    }?: {
      bufferLimit?: number | string,
      retryCount?: number,
      retryDelay?: number | string,
      connectTimeout?: number | string,
      readTimeout?: number | string,
      writeTimeout?: number | string,
      idleTimeout?: number | string,
    }
  ): Configuration;

  /**
   * Appends a _connectHTTPTunnel_ filter to the current pipeline layout.
   *
   * A _connectHTTPTunnel_ filter implements HTTP tunnel on the client side.
   *
   * - **INPUT** - _Data_ stream to send to the server via HTTP tunnel.
   * - **OUTPUT** - _Data_ stream received from the server via HTTP tunnel.
   * - **SUB-INPUT** - _Data_ stream to send to the server with a leading HTTP CONNECT request _Message_.
   * - **SUB-OUTPUT** - _Data_ stream received from the server with a leading HTTP CONNECT response _Message_.
   */
  connectHTTPTunnel(target: string | (() => string)): Configuration;

  /**
   * Appends a _connectSOCKS_ filter to the current pipeline layout.
   *
   * A _connectSOCKS_ filter implements SOCKS protocol on the client side.
   *
   * - **INPUT** - _Data_ stream to send to the server via SOCKS.
   * - **OUTPUT** - _Data_ stream received from the server via SOCKS.
   * - **SUB-INPUT** - _Data_ stream to send to the server with a leading SOCKS connection request.
   * - **SUB-OUTPUT** - _Data_ stream received from the server with a leading SOCKS connection response.
   */
  connectSOCKS(target: string | (() => string)): Configuration;

  /**
   * Appends a connectTLS filter to the current pipeline layout.
   *
   * A connectTLS filter implements TLS protocol on the client side.
   */
  connectTLS(
    options?: {
      certificate?: {
        key: crypto.PrivateKey,
        cert: crypto.Certificate | crypto.CertificateChain,
      } | (
        (sni: string) => {
          key: crypto.PrivateKey,
          cert: crypto.Certificate | crypto.CertificateChain,
        }
      ),
      trusted?: crypto.Certificate[],
      alpn?: string | string[],
      sni?: string | (() => string),
      handshake?: (protocolName: string | undefined) => void,
    }
  ): Configuration;

  /**
   * Appends a _decodeDubbo_ filter to the current pipeline layout.
   *
   * A _decodeDubbo_ filter decodes Dubbo messages from a raw byte stream.
   *
   * - **INPUT** - _Data_ stream carrying Dubbo messages.
   * - **OUTPUT** - Dubbo _Messages_ decoded from the input _Data_ stream.
   */
  decodeDubbo(): Configuration;

  /**
   * Appends a _decodeHTTPRequest_ filter to the current pipeline layout.
   *
   * A _decodeHTTPRequest_ filter decodes HTTP/1 request messages from a raw byte stream.
   *
   * - **INPUT** - _Data_ stream carrying HTTP/1 request messages.
   * - **OUTPUT** - HTTP/1 request _Messages_ decoded from the input _Data_ stream.
   */
   decodeHTTPRequest(): Configuration;


  /**
   * Appends a _decodeHTTPResponse_ filter to the current pipeline layout.
   *
   * A _decodeHTTPResponse_ filter decodes HTTP/1 response messages from a raw byte stream.
   *
   * - **INPUT** - _Data_ stream carrying HTTP/1 response messages.
   * - **OUTPUT** - HTTP/1 response _Messages_ decoded from the input _Data_ stream.
   */
   decodeHTTPResponse(
    { bodiless = false }: { bodiless?: boolean | (() => boolean) }
   ): Configuration;

  /**
   * Appends a _decodeMQTT_ filter to the current pipeline layout.
   *
   * A _decodeMQTT_ filter decodes MQTT packets from a raw byte stream.
   *
   * - **INPUT** - _Data_ stream carrying MQTT packets.
   * - **OUTPUT** - MQTT packets _(Messages)_ decoded from the input _Data_ stream.
   */
  decodeMQTT(
    { protocolLevel = 4 }: { protocolLevel?: number | (() => number) }
  ): Configuration;

  /**
   * Appends a _decodeMultipart_ filter to the current pipeline layout.
   *
   * A _decodeMultipart_ filter decodes parts from MIME multipart messages.
   *
   * - **INPUT** - _Messages_ in MIME multipart format.
   * - **OUTPUT** - Parts _(Messages)_ decoded from the input MIME multipart messages.
   */
  decodeMultipart(): Configuration;

  /**
   * Appends a _decodeWebSocket_ filter to the current pipeline layout.
   *
   * A _decodeWebSocket_ filter decodes WebSocket messages from a raw byte stream.
   *
   * - **INPUT** - _Data_ stream carrying WebSocket messages.
   * - **OUTPUT** - WebSocket _Messages_ decoded from the input _Data_ stream.
   */
  decodeWebSocket(): Configuration;

  /**
   * Appends a _decompressHTTP_ filter to the current pipeline layout.
   *
   * A _decompressHTTP_ filter decompresses HTTP messages.
   *
   * - **INPUT** - HTTP _Messages_ to decompress.
   * - **OUTPUT** - Decompressed HTTP _Messages_.
   */
  decompressHTTP(enable?: () => boolean): Configuration;

  /**
   * Appends a _decompressMessage_ filter to the current pipeline layout.
   *
   * A _decompressMessage_ filter decompresses messages.
   *
   * - **INPUT** - _Messages_ to decompress.
   * - **OUTPUT** - Decompressed _Messages_.
   */
  decompressMessage(algorithm: () => '' | 'deflate' | 'gzip' | 'brotli'): Configuration;

  /**
   * Appends a _deframe_ filter to the current pipeline layout.
   */
  deframe(
    states: {
      [state: string]: (data: number | number[] | Data) => (Event | Message | number | number[])[]
    }
  ): Configuration;

  /**
   * Appends a _demux_ filter to the current pipeline layout.
   *
   * A _demux_ filter distributes each input _Message_ to a separate sub-pipeline.
   *
   * - **INPUT** - _Messages_ to distribute to different sub-pipelines.
   * - **OUTPUT** - No output.
   * - **SUB-INPUT** - A _Message_ streaming into the _demux_ filter.
   * - **SUB-OUTPUT** - Disgarded.
   */
  demux(): Configuration;

  /**
   * Appends a _demuxHTTP_ filter to the current pipeline layout.
   *
   * A _demuxHTTP_ filter implements HTTP/1 and HTTP/2 protocol on the server side.
   *
   * - **INPUT** - _Data_ stream received from the client with HTTP/1 or HTTP/2 requests.
   * - **OUTPUT** - _Data_ stream to send to the client with HTTP/1 or HTTP/2 responses.
   * - **SUB-INPUT** - HTTP request _Message_ received from the client.
   * - **SUB-OUTPUT** - HTTP response _Message_ to send to the client.
   */
  demuxHTTP({
    bufferSize = 16384
  }? : {
    bufferSize: number | string
  }): Configuration;

  /**
   * Appends a _demuxQueue_ filter to the current pipeline layout.
   *
   * A _demuxQueue_ filter distributes each input _Message_ to a separate sub-pipeline
   * and outputs _Messages_ coming out from those sub-pipelines in the same order as in the input.
   *
   * - **INPUT** - _Messages_ to distribute to different sub-pipelines.
   * - **OUTPUT** - _Messages_ coming out from the sub-pipelines.
   * - **SUB-INPUT** - A _Message_ streaming into the _demuxQueue_ filter.
   * - **SUB-OUTPUT** - A _Message_ to stream out the _demuxQueue_ filter.
   */
  demuxQueue(): Configuration;
}
