/// <reference path="./Event.d.ts" />
/// <reference path="./Message.d.ts" />
/// <reference path="./Inbound.d.ts" />
/// <reference path="./algo.d.ts" />
/// <reference path="./crypto.d.ts" />

interface ListenOptions {
  protocol?: 'tcp' | 'udp',
  maxConnections?: number,
  readTimeout?: number | string,
  writeTimeout?: number | string,
  idleTimeout?: number | string,
  congestionLimit?: number | string,
  bufferLimit?: number | string,
  keepAlive?: boolean,
  noDelay?: boolean,
  transparent?: boolean,
  masquerade?: boolean,
  peerStats?: boolean,
}

interface MuxSessionOptions {
  maxIdle?: number | string,
  maxQueue?: number,
  maxMessages?: number,
}

interface MuxOptions extends MuxSessionOptions {
  outputCount?: number | (() => number),
}

interface MuxHTTPOptions extends MuxOptions {
  bufferSize?: number | string,
  maxHeaderSize?: number | string,
  version?: number | string | (() => number | string),
}

interface CertificateOptions {
  key: PrivateKey,
  cert: Certificate | CertificateChain,
}

interface PqcOptions {
  /**
   * Key exchange algorithm for post-quantum cryptography.
   * Defaults to "ML-KEM-512" if not specified and pqc is enabled.
   * Available algorithms depend on OpenSSL version:
   * - OpenSSL >= 3.5: Only key exchange algorithms supported
   * - OpenSSL >= 3.2 && < 3.5: Both key exchange and signature algorithms supported
   */
  keyExchange?: "ML-KEM-512" | "ML-KEM-768" | "ML-KEM-1024" | string,
  
  /**
   * Signature algorithm for post-quantum cryptography.
   * Defaults to "ML-DSA-44" if not specified and pqc is enabled.
   * Only available when OpenSSL version >= 3.2 && < 3.5.
   */
  signature?: "ML-DSA-44" | "ML-DSA-65" | "ML-DSA-87" | "SLH-DSA-128s" | "SLH-DSA-128f" | "SLH-DSA-192s" | "SLH-DSA-192f" | "SLH-DSA-256s" | "SLH-DSA-256f" | string,
  
  /**
   * Whether to use hybrid algorithms (combining classical and post-quantum algorithms).
   * Defaults to true.
   */
  hybrid?: boolean,
}

interface ProxyProtocolHeader {
  version?: 1 | 2,
  command?: 'PROXY' | 'LOCAL',
  protocol?: 'TCP4' | 'TCP6' | 'UDP4' | 'UDP6' | 'UNIX' | 'UNIX_DGRAM' | 'UNKNOWN',
  sourceAddress?: string,
  targetAddress?: string,
  sourcePort?: number,
  targetPort?: number,
}

/**
 * Configuration is used to set up context variables and pipeline layouts in a module.
 */
interface Configuration {

  /**
   * Defines context variables that are accessible to other modules.
   *
   * @param namespace The namespace to refer when being imported in other modules.
   * @param variables An object containing key-value pairs of context variable names and their initial values.
   * @returns The same _Configuration_ object.
   */
  export(namespace: string, variables: { [key: string]: any }): Configuration;

  /**
   * Imports context variables defined and exported from other modules.
   *
   * @param variables An object containing key-value pairs of context variable names and their namespaces.
   * @returns The same _Configuration_ object.
   */
  import(variables: { [key: string]: string }): Configuration;

  /**
   * Creates a _port pipeline layout_ for incoming TCP/UDP connections on a port.
   *
   * A _port pipeline_ has the following input/output:
   *
   * - **INPUT** - _Data_ stream received from the client.
   * - **OUTPUT** - _Data_ stream to send to the client.
   *
   * @param port Port number to listen on, or _null_ to skip this pipeline layout.
   * @param options Options including:
   *   - _protocol_ - Can be `"tcp"` or `"udp"`. Default is `"tcp"`.
   *   - _maxPacketSize_ - Maximum packet size when using UDP. Default is 16KB.
   *   - _maxConnections_ - Maximum number of concurrent connections. Default is -1, which means _unlimited_.
   *   - _readTimeout_ - Timeout duration for reading.
   *       Can be a number in seconds or a string with one of the time unit suffixes such as `s`, `m` or `h`.
   *       Defaults to no timeout (waiting forever).
   *   - _writeTimeout_ - Timeout duration for writing.
   *       Can be a number in seconds or a string with one of the time unit suffixes such as `s`, `m` or `h`.
   *       Defaults to no timeout (waiting forever).
   *   - _idleTimeout_ - Time in seconds before connection is closed due to no active reading or writing.
   *       Can be a number in seconds or a string with one of the time unit suffixes such as `s`, `m` or `h`.
   *       Defaults to 1 minute.
   *   - _transparent_ - Set to _true_ to enable [transparent proxy](https://en.wikipedia.org/wiki/Proxy_server#Transparent_proxy) mode,
   *       where the original destination address and port can be found through `__inbound.destinationAddress` and `__inbound.destinationPort` properties.
   *       This is only available on Linux by using NAT or TPROXY.
   *   - _masquerade_ - Set to _true_ to change the source address of responding UDP packets to the original destination.
   * @returns The same _Configuration_ object.
   */
  listen(port: number | null, options?: ListenOptions): Configuration;

  /**
   * Creates a _timer pipeline layout_ or a _signal pipeline layout_ for a periodic job or a signal.
   *
   * A _timer pipeline_ or a _signal pipeline_ has the following input/output:
   *
   * - **INPUT** - Nothing.
   * - **OUTPUT** - Discarded.
   *
   * @param intervalOrSignal Can be either:
   *   - _Nothing (undefined)_ - Create a pipeline only at startup time.
   *   - _A time duration_ - Create a pipeline regularly every specified amount of time.
   *       Can be a number in seconds or a string with one of the time unit suffixes such as `s`, `m` or `h`.
   *   - _A signal name_ - Create a pipeline when receiving a signal, e.g. `"SIGHUP"`, `"SIGINT"`.
   * @returns The same _Configuration_ object.
   */
  task(intervalOrSignal?: number | string): Configuration;

  /**
   * Creates a codebase file watcher.
   *
   * @returns The same _Configuration_ object.
   */
  watch(filename: string): Configuration;

  /**
   * Creates a process exit handler.
   *
   * @returns The same _Configuration_ object.
   */
  exit(): Configuration;

  /**
   * Creates a custom administration handler.
   *
   * @returns The same _Configuration_ object.
   */
  admin(path: string): Configuration;

  /**
   * Creates a _sub-pipeline layout_.
   *
   * A _sub-pipeline_ has the following input/output:
   *
   * - **INPUT** - Any types of _Events_.
   * - **OUTPUT** - Any types of _Events_.
   *
   * @param name The name of the sub-pipeline, or the _module entry_ pipeline layout is created if the name is absent.
   * @returns The same _Configuration_ object.
   */
  pipeline(name?: string): Configuration;

  /**
   * Registers a function to be called when a pipeline is created.
   *
   * @param handler A function that is called every time a new pipeline instance is created.
   *   Its parameters depend on the type of joint-filter that creates the pipeline.
   *   Its return value, if any, is an _Event_ or a _Message_ or an array of them that makes up the initial input to the pipeline.
   * @returns The same _Configuration_ object.
   */
  onStart(handler: () => Event | Message | (Event|Message)[]): Configuration;

  /**
   * Registers a function to be called when a pipeline is destroyed.
   *
   * @param handler A function that is called every time the pipeline instance is destroyed.
   * @returns The same _Configuration_ object.
   */
  onEnd(handler: () => void): Configuration;

  /**
   * Attaches a sub-pipeline layout to the last joint filter.
   *
   * @param pipelineLayout The name of a sub-pipeline layout, or a function that receives a _Configuration_ object
   *   for configuring an anonymous sub-pipeline layout.
   * @returns The same _Configuration_ object.
   */
  to(pipelineLayout: string | ((pipelineConfigurator: Configuration) => void)): Configuration;

  /**
   * Appends an _acceptHTTPTunnel_ filter to the current pipeline layout.
   *
   * An _acceptHTTPTunnel_ filter implements [HTTP tunnel](https://en.wikipedia.org/wiki/HTTP_tunnel) on the server side.
   *
   * - **INPUT** - _Data_ stream received from the client with a leading HTTP CONNECT request _Message_.
   * - **OUTPUT** - _Data_ stream to send to the client with a leading HTTP CONNECT response _Message_.
   * - **SUB-INPUT** - _Data_ stream received from the client via HTTP tunnel.
   * - **SUB-OUTPUT** - _Data_ stream to send to the client via HTTP tunnel.
   *
   * @param handler A function that receives the starting request _Message_ and returns a response _Message_.
   * @returns The same _Configuration_ object.
   */
  acceptHTTPTunnel(handler: (request: Message) => Message): Configuration;

  /**
   * Appends an _acceptProxyProtocol_ filter to the current pipeline layout.
   *
   * An _acceptProxyProtocol_ filter implements the [Proxy Protocol](https://github.com/haproxy/haproxy/blob/master/doc/proxy-protocol.txt) on the server side.
   *
   * - **INPUT** - _Data_ stream received from the client with a Proxy Protocol header.
   * - **OUTPUT** - _Data_ stream to send back to the client.
   * - **SUB-INPUT** - _Data_ stream received from the client with the Proxy Protocol header removed.
   * - **SUB-OUTPUT** - _Data_ stream to send back to the client.
   *
   * @param handler A function that receives the Proxy Protocol header and returns a boolean determining whether the connection should be accepted.
   *   The received header includes the following fields:
   *   - _version_ - Could be 1 or 2
   *   - _command_ - Could be `"PROXY"` or `"LOCAL"` for version 2 only
   *   - _protocol_ - One of `"TCP4"`, `"TCP6"`, `"UDP4"`, `"UDP6"`, `"UNIX"`, `"UNIX_DGRAM"` and `"UNKNOWN"`
   *   - _sourceAddress_ - Source IP address
   *   - _sourcePort_ - Source port
   *   - _targetAddress_ - Destination IP address
   *   - _targetPort_ - Destination port
   * @returns The same _Configuration_ object.
   */
  acceptProxyProtocol(handler: (header: ProxyProtocolHeader) => boolean): Configuration;

  /**
   * Appends an _acceptSOCKS_ filter to the current pipeline layout.
   *
   * An _acceptSOCKS_ filter implements [SOCKS](https://en.wikipedia.org/wiki/SOCKS) protocol on the server side.
   *
   * - **INPUT** - _Data_ stream received from the client with a leading SOCKS connection request.
   * - **OUTPUT** - _Data_ stream to send to the client with a leading SOCKS connection response.
   * - **SUB-INPUT** - _Data_ stream received from the client via SOCKS.
   * - **SUB-OUTPUT** - _Data_ stream to send to the client via SOCKS.
   *
   * @param handler A function that receives _host_, _port_ and _username_ of the SOCKS request and
   *   returns _true_ to accept the connection or _false_ to refuse it
   * @returns The same _Configuration_ object.
   */
  acceptSOCKS(handler: (host, port, id) => boolean): Configuration;

  /**
   * Appends an _acceptTLS_ filter to the current pipeline layout.
   *
   * An _acceptTLS_ filter implements [TLS](https://en.wikipedia.org/wiki/Transport_Layer_Security) protocol on the server side.
   *
   * - **INPUT** - TLS-encrypted _Data_ stream received from the client.
   * - **OUTPUT** - TLS-encrypted _Data_ stream to send to the client.
   * - **SUB-INPUT** - _Data_ stream received from the client after TLS decryption.
   * - **SUB-OUTPUT** - _Data_ stream to send to the client before TLS encryption.
   *
   * @param options Options including:
   *   - _certificate_ - (required) An object containing _cert_ and _key_ or a function that returns such an object
   *       after receiving _sni_ for the requested server name.
   *       In both cases, _cert_ can be a _crypto.Certificate_ or a _crypto.CertificateChain_
   *       and _key_ must be a _crypto.PrivateKey_.
   *   - _trusted_ - (optional) An array of _crypto.Certificate_ objects for allowed client certificates.
   *   - _verify_ - (optional) A callback function that gets called for each certificate in the peer's certificate chain.
   *       It receives 2 arguments: _ok_ and _cert_, where _ok_ is a boolean indicating whether pre-verification is fine,
   *       and _cert_ is the _Certificate_ object being verified.
   *       It is expected to return `true` if verification should go on, or `false` if the TLS connection should be rejected.
   *   - _alpn_ - (optional) An array of allowed protocol names, or a function that receives an array of client-preferred protocol names
   *       and returns the index of the server-chosen protocol in that array.
   *   - _handshake_ - (optional) A callback function that receives the negotiated protocol name after handshake.
   * @returns The same _Configuration_ object.
   */
  acceptTLS(
    options?: {
      certificate?: CertificateOptions | ((sni: string) => CertificateOptions),
      trusted?: Certificate[],
      verify?: (ok: boolean, cert: Certificate) => boolean,
      alpn?: string[] | ((protocolNames: string[]) => number),
      handshake?: (protocolName: string | undefined) => void,
      pqc?: PqcOptions | (() => PqcOptions),
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
   *
   * @param condition A function that returns _true_ to select the sub-pipeline layout coming after
   * @param pipelineLayout The name of a sub-pipeline layout, or a function that receives a _Configuration_ object
   *   for configuring an anonymous sub-pipeline layout.
   * @param restBranches Other _condition/pipelineLayout_ pairs to select from, with an optional
   *   single _pipelineLayout_ at the end for the fallback branch.
   * @returns The same _Configuration_ object.
   */
  branch(
    condition: () => boolean,
    pipelineLayout: string|((pipelineConfigurator: Configuration) => void),
    ...restBranches: ((() => boolean)|string|((pipelineConfigurator: Configuration) => void))[]
  ): Configuration;

  /**
   * Appends a _branchMessageStart_ filter to the current pipeline layout.
   *
   * @returns The same _Configuration_ object.
   */
  branchMessageStart(
    condition: (evt: MessageStart) => boolean,
    pipelineLayout: string|((pipelineConfigurator: Configuration) => void),
    ...restBranches: (((evt: MessageStart) => boolean)|string|((pipelineConfigurator: Configuration) => void))[]
  ): Configuration;

  /**
   * Appends a _branchMessage_ filter to the current pipeline layout.
   *
   * @returns The same _Configuration_ object.
   */
  branchMessage(
    condition: (msg: Message) => boolean,
    pipelineLayout: string|((pipelineConfigurator: Configuration) => void),
    ...restBranches: (((msg: Message) => boolean)|string|((pipelineConfigurator: Configuration) => void))[]
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
   *
   * @param modules An array of module filenames.
   * @returns The same _Configuration_ object.
   */
  chain(modules?: string[]): Configuration;

  /**
   * Appends a _compress_ filter to the current pipeline layout.
   *
   * A _compress_ filter compresses messages.
   *
   * - **INPUT** - _Messages_ to compress.
   * - **OUTPUT** - Compressed _Messages_.
   *
   * @param algorithm Compression algorithm or a function that returns the compression algorithm.
   *       Available compression algorithms are `"deflate"`, `"gzip"`.
   * @returns The same _Configuration_ object.
   */
  compress(algorithm: string | (() => string)): Configuration;

  /**
   * Appends a _compressHTTP_ filter to the current pipeline layout.
   *
   * A _compressHTTP_ filter compresses HTTP messages.
   *
   * - **INPUT** - HTTP _Messages_ to compress.
   * - **OUTPUT** - Compressed HTTP _Messages_.
   *
   * @param options Options including:
   *   - method - Compression method or a function that returns the compression method.
   *       Available compression methods are `"deflate"`, `"gzip"` and  `""` for no compression.
   *       Default is `""`.
   *   - level - Compression level or a function that returns the compression level.
   *       Available compression levels are `"default"`, `"speed"` and `"best"`.
   *       Default is `"default"`.
   * @returns The same _Configuration_ object.
   */
  compressHTTP(
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
   *
   * @param target The target to connect to, in form of `"<host>:<port>"`, or a function that returns the target.
   * @param options Options including:
   *   - _protocol_ - Protocol to use. Can be `"TCP"` or `"UDP"`. Default is `"TCP"`.
   *   - _bind_ - A string in form of `"<IP>:<port>"` providing the local IP and port to bind to or a function that returns that.
   *   - _congestionLimit_ - Size threshold of data backlog in output buffer where congestion feedback starts.
   *       Can be a number in bytes or a string with a unit suffix such as `'k'`, `'m'`, `'g'` and `'t'`.
   *   - _bufferLimit_ - Maximum size of data allowed to stay in output buffer as a result of insufficient outbound bandwidth.
   *       Can be a number in bytes or a string with a unit suffix such as `'k'`, `'m'`, `'g'` and `'t'`.
   *   - _retryCount_ - How many times it should retry connection after a failure, or -1 for the infinite retries. Defaults to 0.
   *   - _retryDelay_ - Time duration to wait between connection retries. Defaults to 0.
   *   - _connectTimeout_ - Timeout while connecting.
   *       Can be a number in seconds or a string with one of the time unit suffixes such as `s`, `m` or `h`.
   *       Defaults to no timeout.
   *   - _readTimeout_ - Timeout while reading.
   *       Can be a number in seconds or a string with one of the time unit suffixes such as `s`, `m` or `h`.
   *       Defaults to no timeout.
   *   - _writeTimeout_ - Timeout while writing.
   *       Can be a number in seconds or a string with one of the time unit suffixes such as `s`, `m` or `h`.
   *       Defaults to no timeout.
   *   - _idleTimeout_ - Duration before connection is closed due to no active reading or writing.
   *       Can be a number in seconds or a string with one of the time unit suffixes such as `s`, `m` or `h`.
   *       Defaults to 1 minute.
   *   - _keepAlive_ - Enable sending of keep-alive messages on TCP connections. Defaults to true.
   *   - _noDelay_ - If set, disable the Nagle algorithm. Defaults to true.
   * @returns The same _Configuration_ object.
   */
  connect(
    target: string | (() => string),
    options?: {
      protocol?: string,
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
    }
  ): Configuration;

  /**
   * Appends a _connectHTTPTunnel_ filter to the current pipeline layout.
   *
   * A _connectHTTPTunnel_ filter implements [HTTP tunnel](https://en.wikipedia.org/wiki/HTTP_tunnel) on the client side.
   *
   * - **INPUT** - _Data_ stream to send to the server via HTTP tunnel.
   * - **OUTPUT** - _Data_ stream received from the server via HTTP tunnel.
   * - **SUB-INPUT** - _Data_ stream to send to the server with a leading HTTP CONNECT request _Message_.
   * - **SUB-OUTPUT** - _Data_ stream received from the server with a leading HTTP CONNECT response _Message_.
   *
   * @param handshake The starting CONNECT request, or a function that returns the target.
   * @returns The same _Configuration_ object.
   */
  connectHTTPTunnel(handshake: Message | (() => Message)): Configuration;

  /**
   * Appends a _connectProxyProtocol_ filter to the current pipeline layout.
   *
   * A _connectProxyProtocol_ filter implements the [Proxy Protocol](https://github.com/haproxy/haproxy/blob/master/doc/proxy-protocol.txt) on the client side.
   *
   * - **INPUT** - _Data_ stream to send to the server via the Proxy Protocol.
   * - **OUTPUT** - _Data_ stream received from the server.
   * - **SUB-INPUT** - _Data_ stream to send to the server with the Proxy Protocol header added.
   * - **SUB-OUTPUT** - _Data_ stream received from the server.
   *
   * @param header The Proxy Protocol header, or a function that returns it. The header should include the following fields:
   *   - _version_ - Can be 1 (default) or 2
   *   - _command_ - Can be `"PROXY"` (default) or `"LOCAL"`
   *   - _protocol_ - One of `"TCP4"` (default), `"TCP6"`, `"UDP4"`, `"UDP6"`, `"UNIX"`, `"UNIX_DGRAM"` and `"UNKNOWN"`
   *   - _sourceAddress_ - Source IP address
   *   - _sourcePort_ - Source port
   *   - _targetAddress_ - Destination IP address
   *   - _targetPort_ - Destination port
   * @returns The same _Configuration_ object.
   */
  connectProxyProtocol(header: ProxyProtocolHeader | (() => ProxyProtocolHeader)): Configuration;

  /**
   * Appends a _connectSOCKS_ filter to the current pipeline layout.
   *
   * A _connectSOCKS_ filter implements [SOCKS](https://en.wikipedia.org/wiki/SOCKS) protocol on the client side.
   *
   * - **INPUT** - _Data_ stream to send to the server via SOCKS.
   * - **OUTPUT** - _Data_ stream received from the server via SOCKS.
   * - **SUB-INPUT** - _Data_ stream to send to the server with a leading SOCKS connection request.
   * - **SUB-OUTPUT** - _Data_ stream received from the server with a leading SOCKS connection response.
   *
   * @param address The destination address to connect to, in form of `"<host>:<port>"`, or a function that returns the destination.
   * @returns The same _Configuration_ object.
   */
  connectSOCKS(address: string | (() => string)): Configuration;

  /**
   * Appends a connectTLS filter to the current pipeline layout.
   *
   * A connectTLS filter implements [TLS](https://en.wikipedia.org/wiki/Transport_Layer_Security) protocol on the client side.
   *
   * - **INPUT** - _Data_ stream to send to the server via TLS.
   * - **OUTPUT** - _Data_ stream received from the server via TLS.
   * - **SUB-INPUT** - TLS-encrypted _Data_ stream to send to the server.
   * - **SUB-OUTPUT** - TLS-encrypted _Data_ stream received from the server.
   *
   * @param options Options including:
   *   - _certificate_ - (optional) An object containing _cert_ and _key_ or a function that returns such an object.
   *       In both cases, _cert_ can be a _crypto.Certificate_ or a _crypto.CertificateChain_
   *       and _key must be a _crypto.PrivateKey_.
   *   - _trusted_ - (optional) An array of _crypto.Certificate_ objects for allowed server certificates
   *   - _verify_ - (optional) A callback function that gets called for each certificate in the peer's certificate chain.
   *       It receives 2 arguments: _ok_ and _cert_, where _ok_ is a boolean indicating whether pre-verification is fine,
   *       and _cert_ is the _Certificate_ object being verified.
   *       It is expected to return `true` if verification should go on, or `false` if the TLS connection should be rejected.
   *   - _sni_ - (optional) SNI server name or a function that returns it
   *   - _alpn_ - (optional) Requested protocol name or an array of preferred protocol names
   *   - _handshake_ - (optional) A callback function that receives the negotiated protocol name after handshake.
   * @returns The same _Configuration_ object.
   */
  connectTLS(
    options?: {
      certificate?: CertificateOptions | (() => CertificateOptions),
      trusted?: Certificate[],
      verify?: (ok: boolean, cert: Certificate) => boolean,
      alpn?: string | string[],
      sni?: string | (() => string),
      pqc?: PqcOptions | (() => PqcOptions),
      handshake?: (protocolName: string | undefined) => void,
    }
  ): Configuration;

  /**
   * Appends a _decodeBGP_ filter to the current pipeline layout.
   *
   * @returns The same _Configuration_ object.
   */
  decodeBGP(
    options?: {
      enableAS4?: boolean | (() => boolean),
    }
  ): Configuration;

  /**
   * Appends a _decodeDubbo_ filter to the current pipeline layout.
   *
   * A _decodeDubbo_ filter decodes [Dubbo](https://dubbo.apache.org/) messages from a raw byte stream.
   *
   * - **INPUT** - _Data_ stream to decode Dubbo messages from.
   * - **OUTPUT** - Dubbo _Messages_ decoded from the input _Data_ stream.
   *
   * @returns The same _Configuration_ object.
   */
  decodeDubbo(): Configuration;

  /**
   * Appends a _decodeHTTPRequest_ filter to the current pipeline layout.
   *
   * A _decodeHTTPRequest_ filter decodes HTTP/1 request messages from a raw byte stream.
   *
   * - **INPUT** - _Data_ stream to decode HTTP/1 request messages from.
   * - **OUTPUT** - HTTP/1 request _Messages_ decoded from the input _Data_ stream.
   *
   * @returns The same _Configuration_ object.
   */
  decodeHTTPRequest(handler?: (evt: MessageStart) => void): Configuration;

  /**
   * Appends a _decodeHTTPResponse_ filter to the current pipeline layout.
   *
   * A _decodeHTTPResponse_ filter decodes HTTP/1 response messages from a raw byte stream.
   *
   * - **INPUT** - _Data_ stream to decode HTTP/1 response messages from.
   * - **OUTPUT** - HTTP/1 response _Messages_ decoded from the input _Data_ stream.
   *
   * @returns The same _Configuration_ object.
   */
  decodeHTTPResponse(handler?: (evt: MessageStart) => MessageStart): Configuration;

  /**
   * Appends a _decodeMQTT_ filter to the current pipeline layout.
   *
   * A _decodeMQTT_ filter decodes [MQTT](https://mqtt.org/) packets from a raw byte stream.
   *
   * - **INPUT** - _Data_ stream to decode MQTT packets from.
   * - **OUTPUT** - MQTT packets _(Messages)_ decoded from the input _Data_ stream.
   *
   * @returns The same _Configuration_ object.
   */
  decodeMQTT(): Configuration;

  /**
   * Appends a _decodeMultipart_ filter to the current pipeline layout.
   *
   * A _decodeMultipart_ filter decodes parts from MIME multipart messages.
   *
   * - **INPUT** - _Messages_ to decode as MIME multipart format.
   * - **OUTPUT** - Parts _(Messages)_ decoded from the input MIME multipart messages.
   *
   * @returns The same _Configuration_ object.
   */
  decodeMultipart(): Configuration;

  /**
   * Appends a _decodeRESP_ filter to the current pipeline layout.
   *
   * @returns The same _Configuration_ object.
   */
  decodeRESP(): Configuration;

  /**
   * Appends a _decodeThrift_ filter to the current pipeline layout.
   *
   * A _decodeThrift_ filter decodes [Thrift](https://thrift.apache.org/) messages from a raw byte stream.
   *
   * - **INPUT** - _Data_ stream to decode Thrift messages from.
   * - **OUTPUT** - Thrift _Messages_ decoded from the input _Data_ stream.
   *
   * @returns The same _Configuration_ object.
   */
  decodeThrift(): Configuration;

  /**
   * Appends a _decodeWebSocket_ filter to the current pipeline layout.
   *
   * A _decodeWebSocket_ filter decodes [WebSocket](https://en.wikipedia.org/wiki/WebSocket) messages from a raw byte stream.
   *
   * - **INPUT** - _Data_ stream to decode WebSocket messages from.
   * - **OUTPUT** - WebSocket _Messages_ decoded from the input _Data_ stream.
   *
   * @returns The same _Configuration_ object.
   */
  decodeWebSocket(): Configuration;

  /**
   * Appends a _decompress_ filter to the current pipeline layout.
   *
   * A _decompress_ filter decompresses input Data stream.
   *
   * - **INPUT** - _Data_ to decompress.
   * - **OUTPUT** - Decompressed _Data_.
   *
   * @param algorithm Algorithm used in decompression.
   *   Available algorithms include `"inflate"`, `"brotli"`.
   *   Can be one of these strings or a function that returns one of them.
   * @returns The same _Configuration_ object.
   */
  decompress(algorithm: string | (() => 'inflate' | 'brotli')): Configuration;

  /**
   * Appends a _decompressHTTP_ filter to the current pipeline layout.
   *
   * A _decompressHTTP_ filter decompresses HTTP messages.
   *
   * - **INPUT** - HTTP _Messages_ to decompress.
   * - **OUTPUT** - Decompressed HTTP _Messages_.
   *
   * @returns The same _Configuration_ object.
   */
  decompressHTTP(): Configuration;

  /**
   * Appends a _deframe_ filter to the current pipeline layout.
   *
   * @param states An object containing key-value pairs of state names and their corresponding handling functions.
   * @returns The same _Configuration_ object.
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
   *
   * @returns The same _Configuration_ object.
   */
  demux(
    options?: {
      outputCount: number | (() => number),
    }
  ): Configuration;

  /**
   * Appends a _demuxHTTP_ filter to the current pipeline layout.
   *
   * A _demuxHTTP_ filter implements HTTP/1 and HTTP/2 protocol on the server side.
   *
   * - **INPUT** - _Data_ stream received from the client with HTTP/1 or HTTP/2 requests.
   * - **OUTPUT** - _Data_ stream to send to the client with HTTP/1 or HTTP/2 responses.
   * - **SUB-INPUT** - HTTP request _Message_ received from the client.
   * - **SUB-OUTPUT** - HTTP response _Message_ to send to the client.
   *
   * @param options Options including:
   *   - _bufferSize_ - (optional) Maximum body size above which a message should be transferred in chunks.
   *       Can be a number in bytes or a string with a unit suffix such as `'k'`, `'m'`, `'g'` and `'t'`.
   *       Default is _16KB_.
   * @returns The same _Configuration_ object.
   */
  demuxHTTP(options? : {
    bufferSize: number | string,
    maxHeaderSize: number | string,
  }): Configuration;

  /**
   * Appends a _detectProtocol_ filter to the current pipeline layout.
   *
   * A _detectProtocol_ filter calls a user function to notify what protocol the input _Data_ stream is.
   *
   * - **INPUT** - _Data_ stream to detect protocol for.
   * - **OUTPUT** - The same _Data_ stream as input.
   *
   * @param handler A function that receives the detected protocol.
   *   Its parameter can be `"TLS"` or `"HTTP"`.
   *   When no known protocols can be detected, it receives `""`.
   * @returns The same _Configuration_ object.
   */
  detectProtocol(handler: (protocol: string) => void): Configuration;

  /**
   * Appends a _dummy_ filter to the current pipeline layout.
   *
   * A _dummy_ filter discards all its input _Events_ and outputs nothing.
   *
   * - **INPUT** - Any types of _Events_.
   * - **OUTPUT** - Nothing.
   *
   * @returns The same _Configuration_ object.
   */
  dummy(): Configuration;

  /**
   * Appends a _dump_ filter to the current pipeline layout.
   *
   * A _dump_ filter prints out all its input _Events_ to the standard error.
   *
   * - **INPUT** - Any types of _Events_.
   * - **OUTPUT** - The same _Events_ from the input.
   *
   * @param tag A value to be printed along with the dump output, or a function that returns such a value.
   * @returns The same _Configuration_ object.
   */
  dump(tag?: string | (() => any)): Configuration;

  /**
   * Appends a _encodeBGP_ filter to the current pipeline layout.
   *
   * @returns The same _Configuration_ object.
   */
  encodeBGP(
    options?: {
      enableAS4?: boolean | (() => boolean),
    }
  ): Configuration;

  /**
   * Appends an _encodeDubbo_ filter to the current pipeline layout.
   *
   * An _encodeDubbo_ filter encodes [Dubbo](https://dubbo.apache.org/) messages into a raw byte stream.
   *
   * - **INPUT** - Dubbo _Messages_ to encode.
   * - **OUTPUT** - Encoded _Data_ stream from the input Dubbo messages.
   *
   * @returns The same _Configuration_ object.
   */
  encodeDubbo(): Configuration;

  /**
   * Appends an _encodeHTTPRequest_ filter to the current pipeline layout.
   *
   * An _encodeHTTPRequest_ filter encodes HTTP/1 request messages into a raw byte stream.
   *
   * - **INPUT** - HTTP/1 request _Messages_ to encode.
   * - **OUTPUT** - Encoded _Data_ stream from the input HTTP/1 request messages.
   *
   * @returns The same _Configuration_ object.
   */
  encodeHTTPRequest(): Configuration;

  /**
   * Appends an _encodeHTTPResponse_ filter to the current pipeline layout.
   *
   * An _encodeHTTPResponse_ filter encodes HTTP/1 response messages into a raw byte stream.
   *
   * - **INPUT** - HTTP/1 response _Messages_ to encode.
   * - **OUTPUT** - Encoded _Data_ stream from the input HTTP/1 response messages.
   *
   * @param options Options including:
   *   - _final_ - (optional) A boolean or a function that returns a boolean
   *       indicating whether the response is the last one on this session.
   *       Default is `false`.
   *   - _bodiless_ - (optional) A boolean or a function that returns a boolean
   *       indicating whether the message is a response to a HEAD request.
   *       Default is `false`.
   *   - _bufferSize_ - (optional) Maximum body size above which a message should be transferred in chunks.
   *       Can be a number in bytes or a string with a unit suffix such as `'k'`, `'m'`, `'g'` and `'t'`.
   *       Default is _16KB_.
   * @returns The same _Configuration_ object.
   */
  encodeHTTPResponse(options?: {
    final?: boolean | (() => boolean),
    bodiless?: boolean | (() => boolean),
    bufferSize?: number | string,
  }): Configuration;

  /**
   * Appends an _encodeMQTT_ filter to the current pipeline layout.
   *
   * An _encodeMQTT_ filter encodes [MQTT](https://mqtt.org/) packets into a raw byte stream.
   *
   * - **INPUT** - MQTT packets _(Messages)_ to encode.
   * - **OUTPUT** - Encoded _Data_ stream from the input MQTT packets.
   *
   * @returns The same _Configuration_ object.
   */
  encodeMQTT(): Configuration;

  /**
   * Appends an _encodeRESP_ filter to the current pipeline layout.
   *
   * @returns The same _Configuration_ object.
   */
  encodeRESP(): Configuration;

  /**
   * Appends an _encodeThrift_ filter to the current pipeline layout.
   *
   * An _encodeThrift_ filter encodes [Thrift](https://thrift.apache.org/) messages into a raw byte stream.
   *
   * - **INPUT** - Thrift _Messages_ to encode.
   * - **OUTPUT** - Encoded _Data_ stream from the input Thrift messages.
   *
   * @returns The same _Configuration_ object.
   */
  encodeThrift(): Configuration;

  /**
   * Appends an _encodeWebSocket_ filter to the current pipeline layout.
   *
   * An _encodeWebSocket_ filter encodes [WebSocket](https://en.wikipedia.org/wiki/WebSocket) messages into a raw byte stream.
   *
   * - **INPUT** - WebSocket _Messages_ to encode.
   * - **OUTPUT** - Encoded _Data_ stream from the input WebSocket messages.
   *
   * @returns The same _Configuration_ object.
   */
  encodeWebSocket(): Configuration;

  /**
   * Appends an _exec_ filter to the current pipeline layout.
   *
   * An _exec_ filter starts a child process and links to its standard input and output.
   *
   * - **INPUT** - The child process's standard input _Data_ stream.
   * - **OUTPUT** - The child process's standard output _Data_ stream.
   *
   * @param command A string or a function that returns a string containing the shell command to execute.
   * @returns The same _Configuration_ object.
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
   *
   * @param startupValues An array of _startup values_, or a function that returns that.
   *   Each startup value will be given to a newly created sub-pipeline via the
   *   parameter to its `onStart()` callback.
   * @returns The same _Configuration_ object.
   */
  fork(startupValues?: any[] | (() => any[])): Configuration;

  /**
   * Appends a _handleData_ filter to the current pipeline layout.
   *
   * A _handleData_ filter calls back user scripts every time a _Data_ event is found in the input stream.
   *
   * - **INPUT** - Any types of _Events_.
   * - **OUTPUT** - Same _Events_ as input.
   *
   * @param handler A callback function that receives _Data_ events passing through the filter.
   * @returns The same _Configuration_ object.
   */
  handleData(handler : (evt: Event) => void): Configuration;

  /**
   * Appends a _handleMessage_ filter to the current pipeline layout.
   *
   * A _handleMessage_ filter calls back user scripts every time a complete message _(Message)_ is found in the input stream.
   *
   * - **INPUT** - Any types of _Events_.
   * - **OUTPUT** - Same _Events_ as input.
   *
   * @param handler A callback function that receives _Messages_ passing through the filter.
   * @returns The same _Configuration_ object.
   */
  handleMessage(handler : (msg: Message) => void): Configuration;

  /**
   * Appends a _handleMessageData_ filter to the current pipeline layout.
   *
   * A _handleMessageData_ filter calls back user scripts every time a complete message body _(Data)_ is found in the input stream.
   *
   * - **INPUT** - Any types of _Events_.
   * - **OUTPUT** - Same _Events_ as input.
   *
   * @param handler A callback function that receives message bodies passing through the filter.
   * @returns The same _Configuration_ object.
   */
  handleMessageBody(handler : (body: Data) => void): Configuration;

  /**
   * Appends a _handleMessageEnd_ filter to the current pipeline layout.
   *
   * A _handleMessageEnd_ filter calls back user scripts every time a _MessageEnd_ event is found in the input stream.
   *
   * - **INPUT** - Any types of _Events_.
   * - **OUTPUT** - Same _Events_ as input.
   *
   * @param handler A callback function that receives _MessageEnd_ events passing through the filter.
   * @returns The same _Configuration_ object.
   */
  handleMessageEnd(handler : (evt: MessageEnd) => void): Configuration;

  /**
   * Appends a _handleMessageStart_ filter to the current pipeline layout.
   *
   * A _handleMessageStart_ filter calls back user scripts every time a _MessageStart_ event is found in the input stream.
   *
   * - **INPUT** - Any types of _Events_.
   * - **OUTPUT** - Same _Events_ as input.
   *
   * @param handler A callback function that receives _MessageStart_ events passing through the filter.
   * @returns The same _Configuration_ object.
   */
  handleMessageStart(handler : (evt: MessageStart) => void): Configuration;

  /**
   * Appends a _handleStreamEnd_ filter to the current pipeline layout.
   *
   * A _handleStreamEnd_ filter calls back user scripts every time a _StreamEnd_ event is found in the input stream.
   *
   * - **INPUT** - Any types of _Events_.
   * - **OUTPUT** - Same _Events_ as input.
   *
   * @param handler A callback function that receives _StreamEnd_ events passing through the filter.
   * @returns The same _Configuration_ object.
   */
  handleStreamEnd(handler : (evt: StreamEnd) => void): Configuration;

  /**
   * Appends a _handleStreamStart_ filter to the current pipeline layout.
   *
   * A _handleStreamStart_ filter calls back user scripts for the first _Event_ in the input stream.
   *
   * - **INPUT** - Any types of _Events_.
   * - **OUTPUT** - Same _Events_ as input.
   *
   * @param handler A callback function that receives the first event passing through the filter.
   * @returns The same _Configuration_ object.
   */
  handleStreamStart(handler : (evt: Event) => void): Configuration;

  /**
   * Appends a _handleTLSClientHello_ filter to the current pipeline layout.
   *
   * A _handleTLSClientHello_ filter calls back user scripts when a [TLS client hello message](https://www.rfc-editor.org/rfc/rfc8446.html#section-4.1.2) is found in the input stream.
   *
   * - **INPUT** - Any types of _Events_.
   * - **OUTPUT** - Same _Events_ as input.
   *
   * @param handler A callback function that receives SNI and ALPN information in the _TLS client hello message_ passing through the filter.
   *   The parameter the callback function receives is an object containing the following fields:
   *   - _serverNames_ - An array of server names from [SNI](https://www.rfc-editor.org/rfc/rfc6066#section-3)
   *   - _protocolNames_ - An array of protocol names from [ALPN](https://www.rfc-editor.org/rfc/rfc7301)
   * @returns The same _Configuration_ object.
   */
  handleTLSClientHello(handler: (msg: { serverNames: string[], protocolNames: string[] }) => void): Configuration;

  /**
   * Appends an _insert_ filter to the current pipeline layout.
   *
   * @returns The same _Configuration_ object.
   */
  insert(handler?: () => Event | Message | (Event|Message)[] | void): Configuration;

  /**
   * Appends a _link_ filter to the current pipeline layout.
   *
   * A _link_ filter starts a sub-pipeline and streams events through it.
   *
   * - **INPUT** - Any types of _Events_ to stream into the sub-pipeline.
   * - **OUTPUT** - _Events_ streaming out from the sub-pipeline.
   * - **SUB-INPUT** - _Events_ streaming into the _link_ filter.
   * - **SUB-OUTPUT** - Any types of _Events_.
   *
   * @param pipelineLayoutName The name of the sub-pipeline layout to link to, or a function that returns that.
   * @returns The same _Configuration_ object.
   */
  link(pipelineLayoutName: string | (() => string)): Configuration;

  /**
   * Appends a _loop_ filter to the current pipeline layout.
   *
   * @returns The same _Configuration_ object.
   */
  loop(pipelineLayout: (config: Configuration) => void): Configuration;

  /**
   * Appends a _mux_ filter to the current pipeline layout.
   *
   * Multiple _mux_ filters merge input _Messages_ into a shared sub-pipeline.
   *
   * - **INPUT** - A _Message_ to queue into the shared sub-pipeline.
   * - **OUTPUT** - The same _Message_ as input.
   * - **SUB-INPUT** - _Messages_ from multiple _mux_ filters.
   * - **SUB-OUTPUT** - Discarded.
   *
   * @param sessionSelector A function that returns a key identifiying the shared sub-pipeline to merge messages to.
   * @param options Options or a function that returns the options including:
   *   - _maxIdle_ - Maximum time an idle sub-pipeline should stay around.
   *       Can be a number in seconds or a string with one of the time unit suffixes such as `s`, `m` or `h`.
   *       Defaults is _60 seconds_.
   *   - _maxQueue_ - Maximum number of messages allowed to run concurrently in one sub-pipeline.
   *   - _maxMessages_ - Maximum number of messages allowed to run accumulatively in one sub-pipeline.
   * @returns The same _Configuration_ object.
   */
  mux(
    sessionSelector: () => any,
    options?: MuxOptions | (() => MuxOptions),
  ): Configuration;

  /**
   * Appends a _mux_ filter that merges to the same sub-pipline
   * as other _mux_ filters coming from the same inbound connection.
   *
   * @param options Options or a function that returns the options including:
   *   - _maxIdle_ - Maximum time an idle sub-pipeline should stay around.
   *       Can be a number in seconds or a string with one of the time unit suffixes such as `s`, `m` or `h`.
   *       Defaults is _60 seconds_.
   *   - _maxQueue_ - Maximum number of messages allowed to run concurrently in one sub-pipeline.
   *   - _maxMessages_ - Maximum number of messages allowed to run accumulatively in one sub-pipeline.
   * @returns The same _Configuration_ object.
   */
  mux(
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
   *
   * @param sessionSelector A function that returns a key identifiying the shared sub-pipeline to merge messages to.
   * @param options Options or a function that returns the options including:
   *   - _maxIdle_ - Maximum time an idle sub-pipeline should stay around.
   *       Can be a number in seconds or a string with one of the time unit suffixes such as `s`, `m` or `h`.
   *       Defaults is `60` seconds.
   *   - _maxQueue_ - Maximum number of messages allowed to run concurrently in one sub-pipeline.
   *   - _maxMessages_ - Maximum number of messages allowed to run accumulatively in one sub-pipeline.
   *   - _bufferSize_ - Maximum body size above which a message should be transferred in chunks.
   *       Can be a number in bytes or a string with a unit suffix such as `'k'`, `'m'`, `'g'` and `'t'`.
   *       Default is _16KB_.
   *   - _version_ - Number `1` for HTTP/1 or number `2` for HTTP/2. Can also be a function that returns `1` or `2`.
   * @returns The same _Configuration_ object.
   */
  muxHTTP(
    sessionSelector: () => any,
    options?: MuxHTTPOptions | (() => MuxHTTPOptions),
  ): Configuration;

  /**
   * Appends a _muxHTTP_ filter that merges to the same sub-pipline
   * as other _muxHTTP_ filters coming from the same inbound connection.
   *
   * @param options Options or a function that returns the options including:
   *   - _maxIdle_ - Maximum time an idle sub-pipeline should stay around.
   *       Can be a number in seconds or a string with one of the time unit suffixes such as `s`, `m` or `h`.
   *       Defaults is `60` seconds.
   *   - _maxQueue_ - Maximum number of messages allowed to run concurrently in one sub-pipeline.
   *   - _maxMessages_ - Maximum number of messages allowed to run accumulatively in one sub-pipeline.
   *   - _bufferSize_ - Maximum body size above which a message should be transferred in chunks.
   *       Can be a number in bytes or a string with a unit suffix such as `'k'`, `'m'`, `'g'` and `'t'`.
   *       Default is _16KB_.
   *   - _version_ - Number `1` for HTTP/1 or number `2` for HTTP/2. Can also be a function that returns `1` or `2`.
   * @returns The same _Configuration_ object.
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
   *
   * @param batchSize Number of messages to pack into one. Default is `1`.
   * @param options Options including:
   *   - _vacancy_ - Percentage of spare space letf in the internal storage of packed _Data_ object. Default is `0.5`.
   *   - _interval_ - Maximum time to wait before outputting a batch even if the number of messages is not enough.
   *       Can be a number in seconds or a string with one of the time unit suffixes such as `s`, `m` or `h`.
   *       Default is _5 seconds_.
   * @returns The same _Configuration_ object.
   */
  pack(
    batchSize?: number,
    options?: {
      vacancy?: number,
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
   *
   * @returns The same _Configuration_ object.
   */
  print(): Configuration;

  /**
   * Appends a _read_ filter to the current pipeline layout.
   *
   * A _read_ filter reads from a file and outputs its content as a _Data_ stream.
   *
   * - **INPUT** - Any types of _Events_.
   * - **OUTPUT** - File content as a _Data_ stream.
   *
   * @param filename Pathname to read from or a callback function that returns the pathname.
   *   Can be `"-"` for reading from the standard input.
   * @returns The same _Configuration_ object.
   */
  read(filename: string | (() => string)): Configuration;

  /**
   * Performs batch configuration.
   *
   * @returns The same _Configuration_ object.
   */
  repeat(count: number | any[], cb: (config: Configuration, value: any, i?: number) => void): Configuration;

  /**
   * Appends a _replaceData_ filter to the current pipeline layout.
   *
   * A _replaceData_ filter calls back user scripts to get a replacement for each _Data_ event found in the input stream.
   *
   * - **INPUT** - Any types of _Events_.
   * - **OUTPUT** - Any types of _Events_.
   *
   * @param handler A callback function that receives _Data_ events passing through the filter and returns their replacements.
   *   The replacement can be an _Event_, a _Message_ or an array of them.
   * @returns The same _Configuration_ object.
   */
  replaceData(handler?: (data: Data) => Event | Message | (Event|Message)[] | void): Configuration;

  /**
   * Appends a _replaceMessage_ filter to the current pipeline layout.
   *
   * A _replaceMessage_ filter calls back user scripts to get a replacement for each complete message _(Message)_ found in the input stream.
   *
   * - **INPUT** - Any types of _Events_.
   * - **OUTPUT** - Any types of _Events_.
   *
   * @param handler A callback function that receives _Messages_ passing through the filter and returns their replacements.
   *   The replacement can be an _Event_, a _Message_ or an array of them.
   * @returns The same _Configuration_ object.
   */
  replaceMessage(handler?: (msg: Message) => Event | Message | (Event|Message)[] | void): Configuration;

  /**
   * Appends a _replaceMessageBody_ filter to the current pipeline layout.
   *
   * A _replaceMessageBody_ filter calls back user scripts to get a replacement for each complete message body _(Data)_ found in the input stream.
   *
   * - **INPUT** - Any types of _Events_.
   * - **OUTPUT** - Any types of _Events_.
   *
   * @param handler A callback function that receives message bodies passing through the filter and returns their replacements.
   *   The replacement can be an _Event_, a _Message_ or an array of them.
   * @returns The same _Configuration_ object.
   */
  replaceMessageBody(handler?: (data: Data) => Event | Message | (Event|Message)[] | void): Configuration;

  /**
   * Appends a _replaceMessageEnd_ filter to the current pipeline layout.
   *
   * A _replaceMessageEnd_ filter calls back user scripts to get a replacement for each _MessageEnd_ event found in the input stream.
   *
   * - **INPUT** - Any types of _Events_.
   * - **OUTPUT** - Any types of _Events_.
   *
   * @param handler A callback function that receives _MessageEnd_ events passing through the filter and returns their replacements.
   *   The replacement can be an _Event_, a _Message_ or an array of them.
   * @returns The same _Configuration_ object.
   */
  replaceMessageEnd(handler?: (evt: MessageEnd) => Event | Message | (Event|Message)[] | void): Configuration;

  /**
   * Appends a _replaceMessageStart_ filter to the current pipeline layout.
   *
   * A _replaceMessageStart_ filter calls back user scripts to get a replacement for each _MessageStart_ event found in the input stream.
   *
   * - **INPUT** - Any types of _Events_.
   * - **OUTPUT** - Any types of _Events_.
   *
   * @param handler A callback function that receives _MessageStart_ events passing through the filter and returns their replacements.
   *   The replacement can be an _Event_, a _Message_ or an array of them.
   * @returns The same _Configuration_ object.
   */
  replaceMessageStart(handler?: (evt: MessageStart) => Event | Message | (Event|Message)[] | void): Configuration;

  /**
   * Appends a _replaceStreamEnd_ filter to the current pipeline layout.
   *
   * A _replaceStreamEnd_ filter calls back user scripts to get a replacement for each _StreamEnd_ event found in the input stream.
   *
   * - **INPUT** - Any types of _Events_.
   * - **OUTPUT** - Any types of _Events_.
   *
   * @param handler A callback function that receives _StreamEnd_ events passing through the filter and returns their replacements.
   *   The replacement can be an _Event_, a _Message_ or an array of them.
   * @returns The same _Configuration_ object.
   */
  replaceStreamEnd(handler?: (evt: StreamEnd) => Event | Message | (Event|Message)[] | void): Configuration;

  /**
   * Appends a _replaceStreamStart_ filter to the current pipeline layout.
   *
   * A _replaceStreamStart_ filter calls back user scripts to get a replacement for the first event in the input stream.
   *
   * - **INPUT** - Any types of _Events_.
   * - **OUTPUT** - Any types of _Events_.
   *
   * @param handler A callback function tha receives the first event passing through the filter and returns its replacement.
   *   The replacement can be an _Event_, a _Message_ or an array of them.
   * @returns The same _Configuration_ object.
   */
  replaceStreamStart(handler?: (evt: Event) => Event | Message | (Event|Message)[] | void): Configuration;

  /**
   * Appends a _replay_ filter to the current pipeline layout.
   *
   * A _replay_ filter repeats its input event sequence to a new sub-pipeline
   * when the previous sub-pipeline outputs a StreamEnd event with error code _"Replay"_.
   *
   * - **INPUT** - Any types of _Events_ to stream into the sub-pipelines.
   * - **OUTPUT** - _Events_ streaming out from the sub-pipelines.
   * - **SUB-INPUT** - _Events_ streaming into the _replay_ filter.
   * - **SUB-OUTPUT** - Any types of _Events_.
   *
   * @param options Options including:
   *   - _delay_ - Time interval to wait before each replay.
   *       Can be a number in seconds, or a string with a time unit suffix like `'s'`, `'m'` or `'h'`,
   *       or a function that returns that.
   * @returns The same _Configuration_ object.
   */
  replay(options?: { delay?: number | string | (() => number | string) }): Configuration;

  /**
   * Appends a _serveHTTP_ filter to the current pipeline layout.
   *
   * A _serveHTTP_ filter calls back user scripts to get an output HTTP response for each HTTP request found in the input stream.
   *
   * - **INPUT** - _Data_ stream containing HTTP requests received from the client.
   * - **OUTPUT** - _Data_ stream containing HTTP responses to send to the client.
   *
   * @param handler A callback function that receives a request _Message_ and returns the corresponding response _Message_.
   * @returns The same _Configuration_ object.
   */
  serveHTTP(handler: (request: Message) => Message | Promise<Message>): Configuration;

  /**
   * Appends a _split_ filter to the current pipeline layout.
   *
   * A _split_ filter splits an input _Message_ into multiple _Messages_ by a given separator.
   *
   * - **INPUT** - _Messages_ to split.
   * - **OUTPUT** - _Messages_ splitted from the input.
   *
   * @param separator A string or a _Data_ object as the separator, or a function that returns that.
   * @returns The same _Configuration_ object.
   */
  split(separator: string | Data | (() => string | Data)): Configuration;

  /**
   * Appends a _tee_ filter to the current pipeline layout.
   *
   * A _tee_ filter writes input _Data_ to a file.
   *
   * - **INPUT** - Any types of _Events_.
   * - **OUTPUT** - Same _Events_ as the input.
   *
   * @param filename Pathname of the file to write to.
   * @returns The same _Configuration_ object.
   */
  tee(filename: string | (() => string)): Configuration;

  /**
   * Appends a _throttleConcurrency_ filter to the current pipeline layout.
   *
   * A _throttleConcurrency_ filter limits the number of concurrent streams.
   *
   * - **INPUT** - Any types of _Events_.
   * - **OUTPUT** - Same _Events_ as the input.
   *
   * @param quota A _Quota_ object or a function returns it.
   * @returns The same _Configuration_ object.
   */
  throttleConcurrency(quota: Quota | (() => Quota)): Configuration;

  /**
   * Appends a _throttleDataRate_ filter to the current pipeline layout.
   *
   * A _throttleDataRate_ filter limits the amout of _Data_ passing through per unit of time.
   *
   * - **INPUT** - Any types of _Events_.
   * - **OUTPUT** - Same _Events_ as the input.
   *
   * @param quota A _Quota_ object or a function returns it.
   * @returns The same _Configuration_ object.
   */
  throttleDataRate(quota: Quota | (() => Quota)): Configuration;

  /**
   * Appends a _throttleMessageRate_ filter to the current pipeline layout.
   *
   * A _throttleMessageRate_ filter limits the number of _Messages_ passing through per unit of time.
   *
   * - **INPUT** - Any types of _Events_.
   * - **OUTPUT** - Same _Events_ as the input.
   *
   * @param quota A _Quota_ object or a function returns it.
   * @returns The same _Configuration_ object.
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
   *
   * @param filename Module file pathname in the current codebase.
   * @param pipelineLayoutName Name of the sub-pipeline layout in the module being used.
   *   The _entry pipeline_ will be used if the name is not given.
   * @returns The same _Configuration_ object.
   */
  use(filename: string, pipelineLayoutName?: string): Configuration;

  /**
   * Appends a _wait_ filter to the current pipeline layout.
   *
   * A _wait_ filter blocks all input _Events_ up until a condition is met.
   *
   * - **INPUT** - Any types of _Events_.
   * - **OUTPUT** - Same _Events_ as the input
   *
   * @param condition A callback function that returns `true` to let through input events.
   * @returns The same _Configuration_ object.
   */
  wait(condition: () => boolean | Promise<any>): Configuration;
}
