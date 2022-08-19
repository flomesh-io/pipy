/// <reference no-default-lib="true"/>
/// <reference path="./Event.d.ts" />
/// <reference path="./Message.d.ts" />
/// <reference path="./Output.d.ts" />
/// <reference path="./algo.d.ts" />
/// <reference path="./crypto.d.ts" />

interface MuxOptions {
  maxIdle: number | string,
  maxQueue: number,
}

interface MuxHTTPOptions extends MuxOptions {
  bufferSize: number | string,
  version: number | (() => number),
}

interface CertificateOptions {
  key: PrivateKey,
  cert: Certificate | CertificateChain,
}

/**
 * Configuration is used to set up context variables and pipeline layouts in a module.
 */
interface Configuration {

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
      certificate?: CertificateOptions | ((sni: string) => CertificateOptions),
      trusted?: Certificate[],
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
   * - **INPUT** - Any types of _Events_ to stream into the selected sub-pipeline.
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
    options?: {
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
    options?: {
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
    options?: {
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
   *
   * - **INPUT** - _Data_ stream to send to the server via TLS.
   * - **OUTPUT** - _Data_ stream received from the server via TLS.
   * - **SUB-INPUT** - TLS-encrypted _Data_ stream to send to the server.
   * - **SUB-OUTPUT** - TLS-encrypted _Data_ stream received from the server.
   */
  connectTLS(
    options?: {
      certificate?: CertificateOptions | (() => CertificateOptions),
      trusted?: Certificate[],
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
   * - **INPUT** - _Data_ stream to decode Dubbo messages from.
   * - **OUTPUT** - Dubbo _Messages_ decoded from the input _Data_ stream.
   */
  decodeDubbo(): Configuration;

  /**
   * Appends a _decodeHTTPRequest_ filter to the current pipeline layout.
   *
   * A _decodeHTTPRequest_ filter decodes HTTP/1 request messages from a raw byte stream.
   *
   * - **INPUT** - _Data_ stream to decode HTTP/1 request messages from.
   * - **OUTPUT** - HTTP/1 request _Messages_ decoded from the input _Data_ stream.
   */
   decodeHTTPRequest(): Configuration;


  /**
   * Appends a _decodeHTTPResponse_ filter to the current pipeline layout.
   *
   * A _decodeHTTPResponse_ filter decodes HTTP/1 response messages from a raw byte stream.
   *
   * - **INPUT** - _Data_ stream to decode HTTP/1 response messages from.
   * - **OUTPUT** - HTTP/1 response _Messages_ decoded from the input _Data_ stream.
   */
   decodeHTTPResponse(
    options?: { bodiless?: boolean | (() => boolean) }
   ): Configuration;

  /**
   * Appends a _decodeMQTT_ filter to the current pipeline layout.
   *
   * A _decodeMQTT_ filter decodes MQTT packets from a raw byte stream.
   *
   * - **INPUT** - _Data_ stream to decode MQTT packets from.
   * - **OUTPUT** - MQTT packets _(Messages)_ decoded from the input _Data_ stream.
   */
  decodeMQTT(
    options?: { protocolLevel?: number | (() => number) }
  ): Configuration;

  /**
   * Appends a _decodeMultipart_ filter to the current pipeline layout.
   *
   * A _decodeMultipart_ filter decodes parts from MIME multipart messages.
   *
   * - **INPUT** - _Messages_ to decode as MIME multipart format.
   * - **OUTPUT** - Parts _(Messages)_ decoded from the input MIME multipart messages.
   */
  decodeMultipart(): Configuration;

  /**
   * Appends a _decodeWebSocket_ filter to the current pipeline layout.
   *
   * A _decodeWebSocket_ filter decodes WebSocket messages from a raw byte stream.
   *
   * - **INPUT** - _Data_ stream to decode WebSocket messages from.
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
  demuxHTTP(options? : {
    bufferSize: number | string
  }): Configuration;

  /**
   * Appends a _demuxQueue_ filter to the current pipeline layout.
   *
   * A _demuxQueue_ filter distributes each input _Message_ to a separate sub-pipeline
   * and outputs _Messages_ streaming out from those sub-pipelines in the same order as they are in the input.
   *
   * - **INPUT** - _Messages_ to distribute to different sub-pipelines.
   * - **OUTPUT** - _Messages_ streaming out from the sub-pipelines.
   * - **SUB-INPUT** - A _Message_ streaming into the _demuxQueue_ filter.
   * - **SUB-OUTPUT** - A _Message_ to stream out the _demuxQueue_ filter.
   */
  demuxQueue(): Configuration;

  /**
   * Appends a _depositMessage_ filter to the current pipeline layout.
   *
   * A _depositMessage_ filter buffers a whole message body in a temporary file.
   *
   * - **INPUT** - _Message_ to store in a file.
   * - **OUTPUT** - The same _Message_ as input.
   */
  depositMessage(
    filename: string | (() => string),
    options? : {
      threshold?: number | string,
      keep?: boolean,
    }
  ): Configuration;

  /**
   * Appends a _detectProtocol_ filter to the current pipeline layout.
   *
   * A _detectProtocol_ filter calls a user function to notify what protocol the input _Data_ stream is.
   *
   * - **INPUT** - _Data_ stream to detect protocol for.
   * - **OUTPUT** - The same _Data_ stream as input.
   */
  detectProtocol(handler: (protocol: string) => void): Configuration;

  /**
   * Appends a _dummy_ filter to the current pipeline layout.
   *
   * A _dummy_ filter discards all its input _Events_ and outputs nothing.
   *
   * - **INPUT** - Any types of _Events_.
   * - **OUTPUT** - Nothing.
   */
  dummy(): Configuration;

  /**
   * Appends a _dump_ filter to the current pipeline layout.
   *
   * A _dump_ filter prints out all its input _Events_ to the standard error.
   *
   * - **INPUT** - Any types of _Events_.
   * - **OUTPUT** - The same _Events_ from the input.
   */
  dump(tag?: string | (() => any)): Configuration;

  /**
   * Appends an _encodeDubbo_ filter to the current pipeline layout.
   *
   * An _encodeDubbo_ filter encodes Dubbo messages into a raw byte stream.
   *
   * - **INPUT** - Dubbo _Messages_ to encode.
   * - **OUTPUT** - Encoded _Data_ stream from the input Dubbo messages.
   */
  encodeDubbo(): Configuration;

  /**
   * Appends an _encodeHTTPRequest_ filter to the current pipeline layout.
   *
   * An _encodeHTTPRequest_ filter encodes HTTP/1 request messages into a raw byte stream.
   *
   * - **INPUT** - HTTP/1 request _Messages_ to encode.
   * - **OUTPUT** - Encoded _Data_ stream from the input HTTP/1 request messages.
   */
  encodeHTTPRequest(): Configuration;

  /**
   * Appends an _encodeHTTPResponse_ filter to the current pipeline layout.
   *
   * An _encodeHTTPResponse_ filter encodes HTTP/1 response messages into a raw byte stream.
   *
   * - **INPUT** - HTTP/1 response _Messages_ to encode.
   * - **OUTPUT** - Encoded _Data_ stream from the input HTTP/1 response messages.
   */
  encodeHTTPResponse(): Configuration;

  /**
   * Appends an _encodeMQTT_ filter to the current pipeline layout.
   *
   * An _encodeMQTT_ filter encodes MQTT packets into a raw byte stream.
   *
   * - **INPUT** - MQTT packets _(Messages)_ to encode.
   * - **OUTPUT** - Encoded _Data_ stream from the input MQTT packets.
   */
  encodeMQTT(): Configuration;

  /**
   * Appends an _encodeWebSocket_ filter to the current pipeline layout.
   *
   * An _encodeWebSocket_ filter encodes WebSocket messages into a raw byte stream.
   *
   * - **INPUT** - WebSocket _Messages_ to encode.
   * - **OUTPUT** - Encoded _Data_ stream from the input WebSocket messages.
   */
  encodeWebSocket(): Configuration;

  /**
   * Appends an _exec_ filter to the current pipeline layout.
   *
   * An _exec_ filter starts a child process and links to its standard input and output.
   *
   * - **INPUT** - The child process's standard input _Data_ stream.
   * - **OUTPUT** - The child process's standard output _Data_ stream.
   */
  exec(command: string | (() => string)): Configuration;

  /**
   * Appends a _fork_ filter to the current pipeline layout.
   *
   * A _fork_ filter clones _Events_ to one or more sub-pipelines.
   *
   * - **INPUT** - Any types of _Events_ to clone.
   * - **OUTPUT** - Same _Events_ as input.
   * - **SUB-INPUT** - Cloned _Events_ from the _fork_ filter's input.
   * - **SUB-OUTPUT** - Discarded.
   */
  fork(initialParameters?: any[] | (() => any[])): Configuration;

  /**
   * Appends a _handleData_ filter to the current pipeline layout.
   *
   * A _handleData_ filter calls back user scripts every time a _Data_ event is found in the input stream.
   *
   * - **INPUT** - Any types of _Events_.
   * - **OUTPUT** - Same _Events_ as input.
   */
  handleData(handler : (evt: Event) => void): Configuration;

  /**
   * Appends a _handleMessage_ filter to the current pipeline layout.
   *
   * A _handleMessage_ filter calls back user scripts every time a complete message _(Message)_ is found in the input stream.
   *
   * - **INPUT** - Any types of _Events_.
   * - **OUTPUT** - Same _Events_ as input.
   */
  handleMessage(handler : (msg: Message) => void): Configuration;

  /**
   * Appends a _handleMessageData_ filter to the current pipeline layout.
   *
   * A _handleMessageData_ filter calls back user scripts every time a complete message body _(Data)_ is found in the input stream.
   *
   * - **INPUT** - Any types of _Events_.
   * - **OUTPUT** - Same _Events_ as input.
   */
  handleMessageBody(handler : (body: Data) => void): Configuration;

  /**
   * Appends a _handleMessageEnd_ filter to the current pipeline layout.
   *
   * A _handleMessageEnd_ filter calls back user scripts every time a _MessageEnd_ event is found in the input stream.
   *
   * - **INPUT** - Any types of _Events_.
   * - **OUTPUT** - Same _Events_ as input.
   */
  handleMessageEnd(handler : (evt: MessageEnd) => void): Configuration;

  /**
   * Appends a _handleMessageStart_ filter to the current pipeline layout.
   *
   * A _handleMessageStart_ filter calls back user scripts every time a _MessageStart_ event is found in the input stream.
   *
   * - **INPUT** - Any types of _Events_.
   * - **OUTPUT** - Same _Events_ as input.
   */
  handleMessageStart(handler : (evt: MessageStart) => void): Configuration;

  /**
   * Appends a _handleStreamEnd_ filter to the current pipeline layout.
   *
   * A _handleStreamEnd_ filter calls back user scripts every time a _StreamEnd_ event is found in the input stream.
   *
   * - **INPUT** - Any types of _Events_.
   * - **OUTPUT** - Same _Events_ as input.
   */
  handleStreamEnd(handler : (evt: StreamEnd) => void): Configuration;

  /**
   * Appends a _handleStreamStart_ filter to the current pipeline layout.
   *
   * A _handleStreamStart_ filter calls back user scripts for the first _Event_ in the input stream.
   *
   * - **INPUT** - Any types of _Events_.
   * - **OUTPUT** - Same _Events_ as input.
   */
  handleStreamStart(handler : (evt: Event) => void): Configuration;

  /**
   * Appends a _handleTLSClientHello_ filter to the current pipeline layout.
   *
   * A _handleTLSClientHello_ filter calls back user scripts when a TLS client hello message is found in the input stream.
   *
   * - **INPUT** - Any types of _Events_.
   * - **OUTPUT** - Same _Events_ as input.
   */
  handleTLSClientHello(handler: (msg: { serverNames: string[], protocolNames: string[] }) => void): Configuration;

  /**
   * Appends an _input_ filter to the current pipeline layout.
   *
   * An _input_ filter starts a sub-pipeline where _Events_ are outputted via _output_ filters.
   *
   * - **INPUT** - Any types of _Events_.
   * - **OUTPUT** - _Events_ outputted from _output_ filters in the sub-pipeline.
   */
  input(callback?: (out: Output) => void): Configuration;

  /**
   * Appends a _link_ filter to the current pipeline layout.
   *
   * A _link_ filter starts a sub-pipeline and streams events through it.
   *
   * - **INPUT** - Any types of _Events_ to stream into the sub-pipeline.
   * - **OUTPUT** - _Events_ streaming out from the sub-pipeline.
   * - **SUB-INPUT** - _Events_ streaming into the _link_ filter.
   * - **SUB-OUTPUT** - Any types of _Events_.
   */
  link(pipelineLayoutName: string): Configuration;

  /**
   * Appends a _mux_ filter to the current pipeline layout.
   *
   * Multiple _mux_ filters queue input _Messages_ into a shared sub-pipeline.
   *
   * - **INPUT** - A _Message_ to queue into the shared sub-pipeline.
   * - **OUTPUT** - The same _Message_ as input.
   * - **SUB-INPUT** - _Messages_ from multiple _mux_ filters.
   * - **SUB-OUTPUT** - Discarded.
   */
  mux(
    target?: () => any,
    options?: MuxOptions | (() => MuxOptions),
  ): Configuration;

  /**
   * Appends a _mux_ filter to the current pipeline layout.
   *
   * Multiple _mux_ filters queue input _Messages_ into a shared sub-pipeline.
   *
   * - **INPUT** - A _Message_ to queue into the shared sub-pipeline.
   * - **OUTPUT** - The same _Message_ as input.
   * - **SUB-INPUT** - _Messages_ from multiple _mux_ filters.
   * - **SUB-OUTPUT** - Discarded.
   */
  mux(
    options?: MuxOptions | (() => MuxOptions),
  ): Configuration;

  /**
   * Appends a _muxQueue_ filter to the current pipeline layout.
   *
   * Multiple _muxQueue_ filters queue input _Messages_ into a shared sub-pipeline
   * as well as dequeue output _Messages_ from the sub-pipeline.
   *
   * - **INPUT** - A _Message_ to queue into the shared sub-pipeline.
   * - **OUTPUT** - A _Message_ dequeued from the shared sub-pipeline.
   * - **SUB-INPUT** - _Messages_ from multiple _muxQueue_ filters.
   * - **SUB-OUTPUT** - _Messages_ to be dequeued by multiple _muxQueue_ filters.
   */
  muxQueue(
    target?: () => any,
    options?: MuxOptions | (() => MuxOptions),
  ): Configuration;

  /**
   * Appends a _muxQueue_ filter to the current pipeline layout.
   *
   * Multiple _muxQueue_ filters queue input _Messages_ into a shared sub-pipeline
   * as well as dequeue output _Messages_ from the sub-pipeline.
   *
   * - **INPUT** - A _Message_ to queue into the shared sub-pipeline.
   * - **OUTPUT** - A _Message_ dequeued from the shared sub-pipeline.
   * - **SUB-INPUT** - _Messages_ from multiple _muxQueue_ filters.
   * - **SUB-OUTPUT** - _Messages_ to be dequeued by multiple _muxQueue_ filters.
   */
  muxQueue(
    options?: MuxOptions | (() => MuxOptions),
  ): Configuration;

  /**
   * Appends a _muxHTTP_ filter to the current pipeline layout.
   *
   * A _muxHTTP_ filter implements HTTP/1 and HTTP/2 protocol on the client side.
   *
   * - **INPUT** - HTTP request _Message_ to send to the server.
   * - **OUTPUT** - HTTP response _Message_ received from the server.
   * - **SUB-INPUT** - _Data_ stream to send to the server with HTTP/1 or HTTP/2 requests.
   * - **SUB-OUTPUT** - _Data_ stream received from the server with HTTP/1 or HTTP/2 responses.
   */
  muxHTTP(
    target?: () => any,
    options?: MuxHTTPOptions | (() => MuxHTTPOptions),
  ): Configuration;

  /**
   * Appends a _muxHTTP_ filter to the current pipeline layout.
   *
   * A _muxHTTP_ filter implements HTTP/1 and HTTP/2 protocol on the client side.
   *
   * - **INPUT** - HTTP request _Message_ to send to the server.
   * - **OUTPUT** - HTTP response _Message_ received from the server.
   * - **SUB-INPUT** - _Data_ stream to send to the server with HTTP/1 or HTTP/2 requests.
   * - **SUB-OUTPUT** - _Data_ stream received from the server with HTTP/1 or HTTP/2 responses.
   */
  muxHTTP(
    options?: MuxHTTPOptions | (() => MuxHTTPOptions),
  ): Configuration;

  /**
   * Appends a _pack_ filter to the current pipeline layout.
   *
   * A _pack_ filter combines multiple input messages into one.
   *
   * - **INPUT** - Stream of _Messages_ to combine.
   * - **OUTPUT** - Stream of combined _Messages_.
   */
  pack(
    batchSize?: number,
    options?: {
      vacancy?: number,
      timeout?: number | string,
      interval?: number | string,
    }
  ): Configuration;

  /**
   * Appends a _print_ filter to the current pipeline layout.
   *
   * A _print_ filter prints _Data_ to the standard error.
   *
   * - **INPUT** - Any types of _Events_.
   * - **OUTPUT** - Same Events as the input.
   */
  print(): Configuration;

  /**
   * Appends a _replaceData_ filter to the current pipeline layout.
   *
   * A _replaceData_ filter calls back user scripts to get a replacement for each _Data_ event found in the input stream.
   *
   * - **INPUT** - Any types of _Events_.
   * - **OUTPUT** - Any types of _Events_.
   */
  replaceData(handler?: (data: Data) => Event | Message | (Event|Message)[] | void): Configuration;

  /**
   * Appends a _replaceMessage_ filter to the current pipeline layout.
   *
   * A _replaceMessage_ filter calls back user scripts to get a replacement for each complete message _(Message)_ found in the input stream.
   *
   * - **INPUT** - Any types of _Events_.
   * - **OUTPUT** - Any types of _Events_.
   */
  replaceMessage(handler?: (msg: Message) => Event | Message | (Event|Message)[] | void): Configuration;

  /**
   * Appends a _replaceMessageBody_ filter to the current pipeline layout.
   *
   * A _replaceMessageBody_ filter calls back user scripts to get a replacement for each complete message body _(Data)_ found in the input stream.
   *
   * - **INPUT** - Any types of _Events_.
   * - **OUTPUT** - Any types of _Events_.
   */
  replaceMessageBody(handler?: (data: Data) => Event | Message | (Event|Message)[] | void): Configuration;

  /**
   * Appends a _replaceMessageEnd_ filter to the current pipeline layout.
   *
   * A _replaceMessageEnd_ filter calls back user scripts to get a replacement for each _MessageEnd_ event found in the input stream.
   *
   * - **INPUT** - Any types of _Events_.
   * - **OUTPUT** - Any types of _Events_.
   */
  replaceMessageEnd(handler?: (evt: MessageEnd) => Event | Message | (Event|Message)[] | void): Configuration;

  /**
   * Appends a _replaceMessageStart_ filter to the current pipeline layout.
   *
   * A _replaceMessageStart_ filter calls back user scripts to get a replacement for each _MessageStart_ event found in the input stream.
   *
   * - **INPUT** - Any types of _Events_.
   * - **OUTPUT** - Any types of _Events_.
   */
  replaceMessageStart(handler?: (evt: MessageStart) => Event | Message | (Event|Message)[] | void): Configuration;

  /**
   * Appends a _replaceStreamEnd_ filter to the current pipeline layout.
   *
   * A _replaceStreamEnd_ filter calls back user scripts to get a replacement for each _StreamEnd_ event found in the input stream.
   *
   * - **INPUT** - Any types of _Events_.
   * - **OUTPUT** - Any types of _Events_.
   */
  replaceStreamEnd(handler?: (evt: StreamEnd) => Event | Message | (Event|Message)[] | void): Configuration;

  /**
   * Appends a _replaceStreamStart_ filter to the current pipeline layout.
   *
   * A _replaceStreamStart_ filter calls back user scripts to get a replacement for the first event in the input stream.
   *
   * - **INPUT** - Any types of _Events_.
   * - **OUTPUT** - Any types of _Events_.
   */
  replaceStreamStart(handler?: (evt: Event) => Event | Message | (Event|Message)[] | void): Configuration;

  /**
   * Appends a _serveHTTP_ filter to the current pipeline layout.
   *
   * A _serveHTTP_ filter calls back user scripts to get an output HTTP response for each HTTP request found in the input stream.
   *
   * - **INPUT** - _Data_ stream containing HTTP requests received from the client.
   * - **OUTPUT** - _Data_ stream containing HTTP responses to send to the client.
   */
  serveHTTP(handler: (request: Message) => Message): Configuration;

  /**
   * Appends a _split_ filter to the current pipeline layout.
   *
   * A _split_ filter splits an input _Message_ into multiple _Messages_ by a given separator.
   *
   * - **INPUT** - _Messages_ to split.
   * - **OUTPUT** - _Messages_ splitted from the input.
   */
  split(separator: string | Data | (() => string | Data)): Configuration;

  /**
   * Appends a _tee_ filter to the current pipeline layout.
   *
   * A _tee_ filter writes input _Data_ to a file.
   *
   * - **INPUT** - Any types of _Events_.
   * - **OUTPUT** - Same _Events_ as the input.
   */
  tee(filename: string | (() => string)): Configuration;

  /**
   * Appends a _throttleConcurrency_ filter to the current pipeline layout.
   *
   * A _throttleConcurrency_ filter limits the number of concurrent streams.
   *
   * - **INPUT** - Any types of _Events_.
   * - **OUTPUT** - Same _Events_ as the input.
   */
  throttleConcurrency(quota: Quota | (() => Quota)): Configuration;

  /**
   * Appends a _throttleDataRate_ filter to the current pipeline layout.
   *
   * A _throttleDataRate_ filter limits the amout of _Data_ passing through per unit of time.
   *
   * - **INPUT** - Any types of _Events_.
   * - **OUTPUT** - Same _Events_ as the input.
   */
  throttleDataRate(quota: Quota | (() => Quota)): Configuration;

  /**
   * Appends a _throttleMessageRate_ filter to the current pipeline layout.
   *
   * A _throttleMessageRate_ filter limits the number of _Messages_ passing through per unit of time.
   *
   * - **INPUT** - Any types of _Events_.
   * - **OUTPUT** - Same _Events_ as the input.
   */
  throttleMessageRate(quota: Quota | (() => Quota)): Configuration;

  /**
   * Appends a _use_ filter to the current pipeline layout.
   *
   * A _use_ filter creates a sub-pipeline from a pipeline layout in a differen module
   * and streams _Events_ through it.
   *
   * - **INPUT** - Any types of _Events_ to stream into the sub-pipeline.
   * - **OUTPUT** - _Events_ streaming out from the sub-pipeline.
   * - **SUB-INPUT** - _Events_ streaming into the _use_ filter.
   * - **SUB-OUTPUT** - Any types of _Events_.
   */
  use(filename: string, pipelineLayoutName?: string): Configuration;

  /**
   * Appends a _wait_ filter to the current pipeline layout.
   *
   * A _wait_ filter blocks all input _Events_ up until a condition is met.
   *
   * - **INPUT** - Any types of _Events_.
   * - **OUTPUT** - Same _Events_ as the input.
   */
  wait(condition: () => boolean): Configuration;
}
