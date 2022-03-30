/**
 * @callback AnyCB
 * @return {*}
 *
 * @callback BooleanCB
 * @return {boolean}
 *
 * @callback NumberCB
 * @return {number}
 *
 * @callback StringCB
 * @return {string}
 *
 * @callback NumberStringCB
 * @return {number|string}
 *
 * @callback RequestHandler
 * @param {Message} request
 * @return {Message}
 *
 * @callback StreamStartHandler
 * @param {Event} request
 *
 * @callback StreamEndHandler
 * @param {StreamEnd} request
 *
 * @callback DataHandler
 * @param {Data} request
 *
 * @callback MessageHandler
 * @param {Message} request
 *
 * @callback MessageStartHandler
 * @param {MessageStart} request
 *
 * @callback MessageEndHandler
 * @param {MessageEnd} request
 *
 * @callback StreamStartReplacer
 * @param {Event} request
 * @return {Event|Event[]|Message|Message[]}
 *
 * @callback StreamEndReplacer
 * @param {StreamEnd} request
 * @return {Event|Event[]|Message|Message[]}
 *
 * @callback DataReplacer
 * @param {Data} request
 * @return {Event|Event[]|Message|Message[]}
 *
 * @callback MessageReplacer
 * @param {Message} request
 * @return {Event|Event[]|Message|Message[]}
 *
 * @callback MessageStartReplacer
 * @param {MessageStart} request
 * @return {Event|Event[]|Message|Message[]}
 *
 * @callback MessageEndReplacer
 * @param {MessageEnd} request
 * @return {Event|Event[]|Message|Message[]}
 */

/**
 * Configuration helps user configure variables, pipeline layouts in a module,
 * as well as how a module interacts with other modules via variables and sub-pipelines.
 * All PipyJS modules are expected to return a Configuration object after being evaluated.
 */
class Configuration {

  /**
   * @typedef {{ key: PrivateKey, cert: Certificate|CertificateChain }} OptionCert
   * @typedef {{ serverNames: string[], protocolNames: string[] }} ClientHello
   */

  /**
   * Defines global variables accessible to other modules.
   *
   * @param {string} namespace Namespace where the variables will be imported from.
   * @param {Object} variables Variable names and their initial values.
   * @returns {Configuration} The same Configuration object.
   */
  export(namespace, variables) {}

  /**
   * Imports global variables defined and exported from other modules.
   *
   * @param {Object.<string, string>} variables Variable names and their namespaces.
   * @returns {Configuration} The same Configuration object.
   */
  import(variables) {}

  /**
   * Creates a pipeline layout to handle incoming TCP connections on a specified port.
   *
   * @param {number|string} port Port or ip+port to listen on.
   * @param {Object} [options] Options including maxConnections, readTimeout, writeTimeout and transparent.
   * @param {number} [options.maxConnections] Maximum number of concurrent connections.
   * @param {number|string} [options.readTimeout] Read timeout in seconds.
   * @param {number|string} [options.writeTimeout] Write timeout in seconds.
   * @param {number|string} [options.transparent] Enable destinationAddress and destinationPort properties.
   * @returns {Configuration} The same Configuration object.
   */
  listen(port) {}

  /**
   * Creates a pipeline layout that executes a periodic job or responds to a specified signal.
   *
   * @param {string} [interval] Interval between recurrences with a time unit suffix.
   * @returns {Configuration} The same Configuration object.
   */
  task(interval) {}

  /**
   * Creates a sub-pipeline layout under a specified name.
   *
   * @param {string} name The name of the sub-pipeline.
   * @returns {Configuration} The same Configuration object.
   */
  pipeline(name) {}

  /**
   * Appends an acceptHTTPTunnel filter to the current pipeline layout.
   *
   * An acceptHTTPTunnel filter implements HTTP Tunnel on the server side.
   * Its input and output are Messages at the beginning and switch to Data after the tunnel is established.
   * Its sub-pipeline's input and output are Data streams.
   *
   * @param {string} layout Name of the pipeline layout based on which sub-pipelines are created.
   * @param {*} handler Callback function that receives an HTTP request and returns an HTTP response.
   * @returns {Configuration} The same Configuration object.
   */
  acceptHTTPTunnel(layout, handler) {}

  /**
   * @callback SOCKSSessionHandler
   * @param {string} host
   * @param {number} port
   * @param {string} id
   * @return {boolean}
   */
 
  /**
   * Appends an acceptSOCKS filter to the current pipeline layout.
   *
   * An acceptSOCKS filter implements SOCKS protocol on the server side.
   * Its input and output are Data streams (usually TCP streams).
   * Its sub-pipeline's input and output are also Data streams.
   *
   * @param {string} layout Name of the pipeline layout based on which a sub-pipeline is created for each SOCKS session.
   * @param {SOCKSSessionHandler} handler Callback function while a client tries to connect via SOCKS.
   * @returns {Configuration} The same Configuration object.
   */
  acceptSOCKS(layout, handler) {}

  /**
   * Appends an acceptTLS filter to the current pipeline layout.
   *
   * An acceptTLS filter implements TLS protocol on the server side.
   * Its input and output are Data streams (usually TCP streams).
   * Its sub-pipeline's input and output are also Data streams.
   *
   * @param {string} layout Name of the pipeline layout based on which a sub-pipeline is created for each TLS session.
   * @param {Object} [options] Options including certificate and trusted.
   * @param {OptionCert | () => OptionCert} [options.certificate] Server certificate and private key.
   * @param {Certificate[]} [options.trusted] List of trusted client certifcates.
   * @returns {Configuration} The same Configuration object.
   */
  acceptTLS(layout, options) {}

  /**
   * Appends a connect filter to the current pipeline layout.
   *
   * A connect filter establishes a TCP connection to a remote host.
   * Its input and output are both TCP streams represented as Data streams.
   *
   * @param {string | StringCB} target Target address to connect to.
   * @param {Object} [options] Options including bufferLimit, retryCount, retryDelay, connectTimeout, readTimeout and writeTimeout.
   * @param {number|string} [options.bufferLimit] Maximum outbound data size that would be buffered when sending is too slow.
   * @param {number} [options.retryCount] Maximum number of retries when connection is unsuccessful.
   * @param {number|string} [options.retryDelay] Interval between retries in seconds.
   * @param {number|string} [options.connectTimeout] Timeout while connecting.
   * @param {number|string} [options.readTimeout] Timeout while reading.
   * @param {number|string} [options.writeTimeout] Timeout while writing.
   * @returns {Configuration} The same Configuration object.
   */
  connect(target, options) {}

  /**
   * Appends a connectHTTPTunnel filter to the current pipeline layout.
   *
   * A connectHTTPTunnel filter implements HTTP tunnel on the client side.
   * Its input and output are Data streams.
   * Its sub-pipeline's input and output are Messages at the beginning and switch to Data after the tunnel is established.
   *
   * @param {string} layout Name of the pipeline layout based on which a sub-pipeline is created for the tunnel.
   * @param {string | StringCB} target Target endpoint to connect to, in form of "host:port".
   * @returns {Configuration} The same Configuration object.
   */
  connectHTTPTunnel(layout, target) {}

  /**
   * Appends a connectSOCKS filter to the current pipeline layout.
   *
   * A connectSOCKS filter implements SOCKS protocol on the client side.
   * Its input and output are Data streams.
   * Its sub-pipeline's input and output are also Data streams (usually TCP streams).
   *
   * @param {string} layout Name of the pipeline layout based on which a sub-pipeline is created for the SOCKS session.
   * @param {string | StrinbCB} destination Destination to connection to, in form of "host:port".
   * @returns {Configuration} The same Configuration object.
   */
  connectSOCKS(layout, destination) {}

  /**
   * Appends a connectTLS filter to the current pipeline layout.
   *
   * A connectTLS filter implements TLS protocol on the client side.
   * Its input and output are Data streams.
   * Its sub-pipeline's input and output are also Data streams (usually TCP streams).
   *
   * @param {string} layout Name of the pipeline layout based on which a sub-pipeline is created for the TLS session.
   * @param {Object} [options] Options including certificate, trusted and sni.
   * @param {OptionCert | () => OptionCert} [options.certificate] Client certificate and private key.
   * @param {Certificate[]} [options.trusted] List of trusted server certificate.
   * @param {string | () => string} [options.sni] Host name for SNI.
   * @returns {Configuration} The same Configuration object.
   */
  connectTLS(layout, options) {}

  /**
   * Appends a decodeDubbo filter to the current pipeline layout.
   *
   * A decodeDubbo filter decodes Dubbo messages in its input Data and outputs Messages after decoding.
   * Its input is a Data stream and its output is a Message stream.
   *
   * @returns {Configuration} The same Configuration object.
   */
  decodeDubbo() {}

  /**
   * Appends a decodeHTTPRequest filter to the current pipeline layout.
   *
   * A decodeHTTPRequest filter decodes HTTP/1.x requests in its input Data and outputs Messages after decoding.
   * Its input is a Data stream and its output is a Message stream.
   *
   * @returns {Configuration} The same Configuration object.
   */
  decodeHTTPRequest() {}

  /**
   * Appends a decodeHTTPResponse filter to the current pipeline layout.
   *
   * A decodeHTTPResponse filter decodes HTTP/1.x responses in its input Data and outputs Messages after decoding.
   * Its input is a Data stream and its output is a Message stream.
   *
   * @param {Object} [options] Options including bodiless.
   * @param {boolean|BooleanCB} [options.bodiless] Callback function that returns true for decoding a response without a body.
   * @returns {Configuration} The same Configuration object.
   */
  decodeHTTPResponse(options) {}

  /**
   * Appends a decodeMQTT filter to the current pipeline layout.
   *
   * A decodeMQTT filter decodes MQTT packets in its input Data and outputs Messages after decoding.
   * Its input is a Data stream and its output is a Message stream.
   *
   * @param {Object} [options] Options including protocolLevel.
   * @param {number|NumberCB} [options.protocolLevel] Callback function that returns the protocol level being used.
   * @returns {Configuration} The same Configuration object.
   */
  decodeMQTT(options) {}

  /**
   * Appends a decompressHTTP filter to the current pipeline layout.
   *
   * A decompressHTTP filter decompresses the bodies of HTTP Messages.
   * Its input and output are both Messages.
   *
   * @param {BooleanCB} [enable] Callback function that decides if a message should be decompressed.
   * @returns {Configuration} The same Configuration object.
   */
  decompressHTTP(enable) {}

  /**
   * Appends a decompressMessage filter to the current pipeline layout.
   *
   * A decompressMessage filter decompresses the bodies of Messages.
   * Its input and output are both Messages.
   *
   * @param {StringCB} algorithm Decompression algorithm to use.
   * @returns {Configuration} The same Configuration object.
   */
  decompressMessage(algorithm) {}

  /**
   * Appends a demux filter to the current pipeline layout.
   *
   * A demux filter de-multiplexes Messages from its input stream,
   * distribute them to many sub-pipelines and
   * multiplexes their output Messages into a single output Message stream.
   * Its input and output are Messages. Its sub-pipelines's input and output are also Messages.
   *
   * @param {string} layout Name of the pipeline layout based on which sub-pipelines are created to handle input Messages.
   * @returns {Configuration} The same Configuration object.
   */
  demux(layout) {}

  /**
   * Appends a demuxQueue filter to the current pipeline layout.
   *
   * A demuxQueue filter de-multiplexes Messages from its input stream,
   * distribute them to many sub-pipelines and
   * multiplexes their output Messages into a single Message stream
   * where the Messages come in the same order as the input.
   * Its input and output are Messages. Its sub-pipelines's input and output are also Messages.
   *
   * @param {string} layout Name of the pipeline layout based on which sub-pipelines are created to handle input Messages.
   * @returns {Configuration} The same Configuration object.
   */
  demuxQueue(layout) {}

  /**
   * Appends a demuxHTTP filter to the current pipeline layout.
   *
   * A demuxHTTP filter de-multiplexes and decodes HTTP requests from its input Data stream,
   * distribute them to many sub-pipelines and
   * encodes and multiplexes their HTTP responses into a single output Data stream.
   * Its input and output are Data (usually TCP streams).
   * Its sub-pipelines's input and output are HTTP Messages.
   *
   * @param {string} layout Name of the pipeline layout based on which sub-pipelines are created to handle HTTP requests.
   * @param {Object} [options] Options including bufferSize.
   * @param {number|string} [options.bufferSize] Response bodies larger than this would be transfered in chunks.
   * @returns {Configuration} The same Configuration object.
   */
  demuxHTTP(layout, options) {}

  /**
   * Appends a depositMessage filter to the current pipeline layout.
   *
   * A depositMessage filter buffers a whole message body in a temporary file. Its input and output are all messages.
   *
   * @param {string|StringCB} filename Pathname of the temporary file.
   * @param {Object} [options] Options including threshold and keep.
   * @param {number|string} [options.threshold] Message body size threshold where message starts to write to the temporary file.
   * @param {boolean} [options.keep] Whether the temporary file should be kept after a message passes through.
   * @returns {Configuration} The same Configuration object.
   */
  depositMessage(filename, options) {}

  /**
   * @callback ProtocolDetectionHandler
   * @param {string} protocolName
   */

  /**
   * Appends a detectProtocol filter to the current pipeline layout.
   *
   * A detectProtocol filter calls a user function to tell whether the input stream is TLS or not.
   * Its input and output can be any kinds of events.
   *
   * @param {ProtocolDetectionHandler} handler Callback function that receives the protocol name.
   * @returns {Configuration} The same Configuration object.
   */
  detectProtocol(handler) {}

  /**
   * Appends a dummy filter to the current pipeline layout.
   *
   * A dummy filter does nothing to its input and outputs nothing either.
   *
   * @returns {Configuration} The same Configuration object.
   */
  dummy() {}

  /**
   * Appends a dump filter to the current pipeline layout.
   *
   * A dump filter prints out all events in the input stream to the standard output.
   * Its input and output can be any kinds of events.
   *
   * @param {*|AnyCB} tag Tag to print prior to the dumped out events.
   * @returns {Configuration} The same Configuration object.
   */
  dump(tag) {}

  /**
   * Appends an encodeDubbo filter to the current pipeline layout.
   *
   * An encodeDubbo filter encodes its input Messages into Dubbo messages and outputs encoded Data.
   * Its input is a Message stream and its output is a Data stream.
   *
   * @param {Object} [head] Message header including id, status, isRequest, isTwoWay and isEvent.
   * @param {number|string} [head.id] Dubbo message ID.
   * @param {number} [head.status] Dubbo message response status.
   * @param {boolean} [head.isRequest] Whether the message is a request or a response.
   * @param {boolean} [head.isTwoWay] Whether the message is two-way.
   * @param {boolean} [head.isEvent] Whether the message is an event.
   * @returns {Configuration} The same Configuration object.
   */
  encodeDubbo(head) {}

  /**
   * Appends an encodeHTTPRequest filter to the current pipeline layout.
   *
   * An encodeHTTPRequest filter encodes its input Messages into HTTP/1.x requests and outputs encoded Data.
   * Its input is a Message stream and its output is a Data stream.
   *
   * @param {Object} [options] Options including bufferSize.
   * @param {number|string} [options.bufferSize=4096] Maximum size for a message not being encoded in chunks.
   * @returns {Configuration} The same Configuration object.
   */
  encodeHTTPRequest(options) {}

  /**
   * Appends an encodeHTTPResponse filter to the current pipeline layout.
   *
   * An encodeHTTPResponse filter encodes its input Messages into HTTP/1.x responses and outputs encoded Data.
   * Its input is a Message stream and its output is a Data stream.
   *
   * @param {Object} [options] Options including final, bodiless and bufferSize.
   * @param {boolean | () => boolean} [final] Decides if the message should be followed by a StreamEnd event.
   * @param {boolean | () => boolean} [bodiless] Decides if the message should have a body.
   * @param {number|string} [options.bufferSize=4096] Maximum size for a message not being encoded in chunks.
   * @returns {Configuration} The same Configuration object.
   */
  encodeHTTPResponse(options) {}

  /**
   * Appends an encodeMQTT filter to the current pipeline layout.
   *
   * An encodeMQTT filter encodes its input Messages into MQTT packets and outputs encoded Data.
   * Its input is a Message stream and its output is a Data stream.
   *
   * @returns {Configuration} The same Configuration object.
   */
  encodeMQTT() {}

  /**
   * Appends an exec filter to the current pipeline layout.
   *
   * An exec filter starts an external process, feeds the input Data to the process's standard input and
   * outputs the Data coming out of the process's standard output.
   * Its input and output are both Data events.
   *
   * @param {string|StringCB} command Shell command to execute.
   * @returns {Configuration} The same Configuration object.
   */
  exec(command) {}

  /**
   * Appends a fork filter to the current pipeline layout.
   *
   * A fork filter creates one or more sub-pipelines,
   * each receiving a clone of all events from the filter's input.
   * The filter's output is exactly the same as its input.
   * All output from the created sub-pipelines are discarded.
   * The filter and its sub-pipelines's input and output can be any types of events.
   *
   * @param {string} layout Name of the pipeline layout based on which sub-pipelines are created.
   * @param {Object[]} [variables] Global variable initial values for each sub-pipeline.
   * @returns {Configuration} The same Configuration object.
   */
  fork(layout, variables) {}

  /**
   * Appends a link filter to the current pipeline layout.
   *
   * A link filter creates a new sub-pipeline and pumps all events through it,
   * as if the new sub-pipeline has taken the place of the link filter.
   * The filter and its sub-pipeline's input and output can be any types of events.
   *
   * @param {string} layout Name of the pipeline layout based on which the sub-pipeline is created.
   * @param {function} [condition] Condition under which the sub-pipeline layout is chosen.
   * @param {string} [defaultLayout] Name of the default pipeline layout when no conditions are met.
   * @returns {Configuration} The same Configuration object.
   */
  link(layout, condition, defaultLayout) {}

  /**
   * Appends a merge filter to the current pipeline layout.
   *
   * A merge filter queues up input Messages into a shared sub-pipeline.
   * Its input and output are Messages.
   * Its sub-pipelines's input is also Messages while their output is simply discarded.
   *
   * @param {string} layout Name of the pipeline layout based on which shared sub-pipelines are created.
   * @param {*} group ID of the group of filters that share the same sub-pipeline.
   * @param {Object} [options] Options including maxIdle.
   * @param {number} [options.maxIdle] Time to wait before an idle shared sub-pipeline is closed.
   * @returns {Configuration} The same Configuration object.
   */
  merge(layout, group, options) {}

  /**
   * Appends a muxQueue filter to the current pipeline layout.
   *
   * A muxQueue filter multiplexes input Messages into a shared sub-pipeline and
   * de-multiplexes output Messages out of it.
   * Its input and output are _Messages_.
   * Its sub-pipelines's input and output are also _Messages_.
   *
   * @param {string} layout Name of the pipeline layout based on which shared sub-pipelines are created.
   * @param {*} group ID of the group of filters that share the same sub-pipeline.
   * @param {Object} [options] Options including maxIdle.
   * @param {number} [options.maxIdle] Time to wait before an unused sub-pipeline is destroyed.
   * @returns {Configuration} The same Configuration object.
   */
  muxQueue(layout, group, options) {}

  /**
   * Appends a muxHTTP filter to the current pipeline layout.
   *
   * A muxHTTP filter encodes and multiplexes HTTP requests into a shared sub-pipeline and
   * de-multiplexes and decodes HTTP responses out of it.
   * Its input and output are HTTP Messages.
   * Its sub-pipelines's input and output are Data (usually TCP streams).
   *
   * @param {string} layout Name of the pipeline layout based on which shared sub-pipelines are created.
   * @param {*} group ID of the group of filters that share the same sub-pipeline.
   * @param {Object} [options] Options including bufferSize, maxIdle, version.
   * @param {number|string} [options.bufferSize] Request bodies larger than this would be transfered in chunks.
   * @param {number|string} [options.maxIdle] Time to wait before an unused sub-pipeline is closed.
   * @param {number|string} [options.version] HTTP protocol version to use. Can be 1 for HTTP/1.1 or 2 for HTTP/2.
   * @returns {Configuration} The same Configuration object.
   */
  muxHTTP(layout, group, options) {}

  /**
   * Appends a handleData filter to the current pipeline layout.
   *
   * A handleData filter calls a user function every time it sees a Data event in the input stream.
   * Its input and output can be any kinds of events.
   *
   * @param {DataHandler} handler Callback function invoked for Data events.
   * @returns {Configuration} The same Configuration object.
   */
  handleData(handler) {}

  /**
   * Appends a handleMessage filter to the current pipeline layout.
   *
   * A handleMessage filter calls a user function every time it sees a complete Message in the input stream.
   * Its input and output can be any kinds of events.
   * 
   * @param {MessageHandler} handler Callback function invoked for whole messages.
   * @returns {Configuration} The same Configuration object.
   */
  handleMessage(handler) {}

  /**
   * Appends a handleMessageBody filter to the current pipeline layout.
   *
   * A handleMessageBody filter calls a user function every time it sees a complete message body in the input stream.
   * Its input and output can be any kinds of events.
   *
   * @param {DataHandler} handler Callback function invoked for whole message bodies.
   * @returns {Configuration} The same Configuration object.
   */
  handleMessageBody(handler) {}

  /**
   * Appends a handleMessageEnd filter to the current pipeline layout.
   *
   * A handleMessageEnd filter calls a user function every time it sees a MessageEnd event in the input stream.
   * Its input and output can be any kinds of events.
   *
   * @param {MessageEndHandler} handler Callback function invoked for MessageEnd event.
   * @returns {Configuration} The same Configuration object.
   */
  handleMessageEnd(handler) {}

  /**
   * Appends a handleMessageStart filter to the current pipeline layout.
   *
   * A handleMessageStart filter calls a user function every time it sees a MessageStart event in the input stream.
   * Its input and output can be any kinds of events.
   *
   * @param {MessageStartHandler} handler Callback function invoked for MessageStart events.
   * @returns {Configuration} The same Configuration object.
   */
  handleMessageStart(handler) {}

  /**
   * Appends a handleStreamEnd filter to the current pipeline layout.
   *
   * A handleStreamEnd filter calls a user function every time it sees a StreamEnd event in the input stream.
   * Its input and output can be any kinds of events.
   *
   * @param {StreamEndHandler} handler Callback function invoked for StreamStart events.
   * @returns {Configuration} The same Configuration object.
   */
  handleStreamEnd(handler) {}

  /**
   * Appends a handleStreamStart filter to the current pipeline layout.
   *
   * A handleStreamStart filter calls a user function when it sees the first event in an input stream.
   * Its input and output can be any kinds of events.
   *
   * @param {EventHandler} handler Callback function invoked for the first event.
   * @returns {Configuration} The same Configuration object.
   */
  handleStreamStart(handler) {}

  /**
   * @callback TLSClientHelloHandler
   * @param {Object} message
   * @param {string[]} message.serverNames
   * @param {string[]} message.protocolNames
   */

  /**
   * Appends a handleTLSClientHello filter to the current pipeline layout.
   *
   * A handleTLSClientHello filter calls a user function when it sees a TLS client hello message at the beginning of an input stream.
   * Its input and output can be any kinds of events.
   *
   * @param {TLSClientHelloHandler} handler Callback function invoked for the ClientHello message.
   * @returns {Configuration} The same Configuration object.
   */
  handleTLSClientHello(handler) {}

  /**
   * Appends a pack filter to the current pipeline layout.
   *
   * A pack filter concatenates Data from multiple input Message bodies into one and outputs the combined Messages.
   * Its input and output are both Message streams.
   *
   * @param {number} [batchSize = 1] Maximum number of messages to combine into one.
   * @param {Object} [options] Options including timeout and vacancy.
   * @param {number|string} [options.timeout] Time to wait for more input messages before outputting a combined one.
   * @param {number} [options.vacancy = 0.5] Maximum allowed vacancy-to-occupancy ratio of the memory chunks used by the message body.
   * @returns {Configuration} The same Configuration object.
   */
  pack(batchSize, options) {}

  /**
   * Appends a print filter to the current pipeline layout.
   *
   * A print filter outputs all Data in the input stream to the standard output.
   * Its input and output can be any kinds of events.
   *
   * @returns {Configuration} The same Configuration object.
   */
  print() {}

  /**
   * Creates a pipeline layout to read and process Data from a file.
   *
   * @param {string} filename Pathname of the input file.
   * @returns {Configuration} The same Configuration object.
   */
  read(filename) {}

  /**
   * Appends a replaceData filter to the current pipeline layout.
   *
   * A replaceData filter calls a user function to get a replacement for every Data event in the input stream.
   * Its input and output can be any kinds of events.
   *
   * @param {DataReplacer} [handler] Callback function that returns a replacement to the input.
   * @returns {Configuration} The same Configuration object.
   */
  replaceData(handler) {}

  /**
   * Appends a replaceMessage filter to the current pipeline layout.
   *
   * A replaceMessage filter calls a user function to get a replacement for every Message in the input stream.
   * Its input and output can be any kinds of events.
   *
   * @param {MessageReplacer} [handler] Callback function that returns a replacement to the input.
   * @returns {Configuration} The same Configuration object.
   */
  replaceMessage(handler) {}

  /**
   * Appends a replaceMessageBody filter to the current pipeline layout.
   *
   * A replaceMessageBody filter calls a user function to get a replacement for every complete message body in the input stream.
   * Its input and output can be any kinds of events.
   *
   * @param {DataReplacer} [handler] Callback function that returns a replacement to the input.
   * @returns {Configuration} The same Configuration object.
   */
  replaceMessageBody(handler) {}

  /**
   * Appends a replaceMessageEnd filter to the current pipeline layout.
   *
   * A replaceMessageEnd filter calls a user function to get a replacement for every MessageEnd event in the input stream.
   * Its input and output can be any kinds of events.
   *
   * @param {MessageEndReplacer} [handler] Callback function that returns a replacement to the input.
   * @returns {Configuration} The same Configuration object.
   */
  replaceMessageEnd(handler) {}

  /**
   * Appends a replaceMessageStart filter to the current pipeline layout.
   *
   * A replaceMessageStart filter calls a user function to get a replacement for every MessageStart event in the input stream.
   * Its input and output can be any kinds of events.
   *
   * @param {MessageStartReplacer} [handler] Callback function that returns a replacement to the input.
   * @returns {Configuration} The same Configuration object.
   */
  replaceMessageStart(handler) {}

  /**
   * Appends a replaceStreamEnd filter to the current pipeline layout.
   *
   * A replaceStreamEnd filter calls a user function to get a replacement for every StreamEnd event in the input stream.
   * Its input and output can be any kinds of events.
   *
   * @param {StreamEndHandler} [handler] Callback function that returns a replacement to the input.
   * @returns {Configuration} The same Configuration object.
   */
  replaceStreamEnd(handler) {}

  /**
   * Appends a replaceStreamStart filter to the current pipeline layout.
   *
   * A replaceStreamStart filter calls a user function to get a replacement for the first event in an input stream.
   * Its input and output can be any kinds of events.
   *
   * @param {EventHandler} [handler] Callback function that returns a replacement to the input.
   * @returns {Configuration} The same Configuration object.
   */
  replaceStreamStart(handler) {}

  /**
   * Appends a serveHTTP filter to the current pipeline layout.
   *
   * A serveHTTP filter de-multiplexes and decodes HTTP requests from its input Data stream,
   * calls a user-defined script handler for each request to get a response and
   * encodes and multiplexes those HTTP responses into a single output Data stream.
   * Its input and output are Data (usually TCP streams).
   *
   * @param {Message | RequestHandler} handler A response message or a callback function that receives a request and returns a response message.
   * @returns {Configuration} The same Configuration object.
   */
  serveHTTP(handler) {}

  /**
   * @callback SplitHandler
   * @param {Number} ubyte
   * @return {undefined|null|Event|Event[]|Message|Message[]}
   */
 
  /**
   * Appends a split filter to the current pipeline layout.
   *
   * A split filter cuts Data objects in pieces and inserts new events in between.
   * Its input is a Data stream. Its output is a stream of Data plus other inserted events.
   *
   * @param {SplitHandler} handler Callback function that decides if Data should be split up after the current byte and what events to insert in between.
   * @returns {Configuration} The same Configuration object.
   */
  split(handler) {}

  /**
   * Appends a tee filter to the current pipeline layout.
   *
   * A tee filter writes Data to a file while passing all events through.
   * Its input and output can be any kinds of events.
   *
   * @param {string|StringCB} filename Pathname of the output file.
   * @returns {Configuration} The same Configuration object.
   */
  tee(filename) {}

  /**
   * Appends a throttleConcurrency filter to the current pipeline layout.
   *
   * A throttleConcurrency filter sets a limit to how many concurrent streams can go through its place in the pipeline layout.
   * Its input and output can be any types of events.
   *
   * @param {number|string|NumberStringCB} quota Number of concurrent streams that are allowed through.
   * @param {*|AnyCB} [account] Name of the account that the quota is entitled to.
   * @returns {Configuration} The same Configuration object.
   */
  throttleConcurrency(quota, account) {}

  /**
   * Appends a throttleDataRate filter to the current pipeline layout.
   *
   * A throttleDataRate filter sets a limit to how many bytes of Data can pass in every second.
   * Its input and output can be any types of events, but the limit only takes effect when they have Data events.
   *
   * @param {number|string|NumberStringCB} quota Amount of data in bytes that are allowed through every second.
   * @param {*|AnyCB} [account] Name of the account that the quota is entitled to.
   * @returns {Configuration} The same Configuration object.
   */
  throttleDataRate(quota, account) {}

  /**
   * Appends a throttleMessageRate filter to the current pipeline layout.
   *
   * A throttleMessageRate filter sets a limit to how many Messages can pass in every second.
   * Its input and output can be any types of events, but the limit only take effect when they have MessageStart events.
   *
   * @param {number|string|NumberStringCB} quota Number of messages allowed through every second.
   * @param {*|AnyCB} [account] Name of the account that the quota is entitled to.
   * @returns {Configuration} The same Configuration object.
   */
  throttleMessageRate(quota, account) {}

  /**
   * Appends a use filter to the current pipeline layout.
   *
   * A use filter pumps events through a sub-pipeline in a different module,
   * or a series of sub-pipelines in a module chain.
   * The filter and its sub-pipelines's input and output can be any types of events.
   *
   * @param {string|string[]} filenames One or more module filenames.
   * @param {string} layout Name of the pipeline layout to process input events in all modules.
   * @param {string} [layoutDown] Name of the pipeline layout to process output events in all modules.
   * @param {BooleanCB} [turnDown] Callback function that decides where the chaining should turn back.
   * @returns {Configuration} The same Configuration object.
   */
  use(filenames, layout, layoutDown, turnDown) {}

  /**
   * Appends a wait filter to the current pipeline layout.
   *
   * A wait filter blocks all input events up until a condition is met.
   * Its input and output can be any types of events.
   *
   * @param {BooleanCB} [condition] Callback function that returns true to unblock events.
   * @returns {Configuration} The same Configuration object.
   */
  wait(condition) {}
}
