/**
 * @callback BooleanCB
 * @return {boolean}
 * 
 * @callback MessageHandler
 * @param {Message} request
 * @return {Message}
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
   * @param {Object} [options] Options including maxConnections, readTimeout and writeTimeout.
   * @param {number} [options.maxConnections] Maximum number of concurrent connections.
   * @param {number|string} [options.readTimeout] Read timeout in seconds.
   * @param {number|string} [options.writeTimeout] Write timeout in seconds.
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
   * Appends a SOCKS server to the current pipeline layout.
   *
   * @param {string} target Name of the sub-pipeline to receive decoded SOCKS streams.
   * @param {(address: string, port: number) => boolean} onConnect Callback function when a client tries to connect.
   * @returns {Configuration} The same Configuration object.
   */
  acceptSOCKS(target, onConnect) {}

  /**
   * Appends a TLS server to the current pipeline layout.
   *
   * @param {string} target Name of the sub-pipeline to receive decoded TLS streams.
   * @param {Object} [options] Options including certificate and trusted.
   * @param {OptionCert | () => OptionCert} [options.certificate] Server certificate and private key.
   * @param {Certificate[]} [options.trusted] List of trusted client certifcates.
   * @returns {Configuration} The same Configuration object.
   */
  acceptTLS(target, options) {}

  /**
   * Appends a TCP client to the current pipeline layout.
   *
   * @param {string | () => string} target Host to connection in form of 'host:port'.
   * @param {Object} [options] Options including bufferLimit, retryCount, retryDelay.
   * @param {number|string} [options.bufferLimit] Maximum outbound data size that would be buffered when sending is too slow.
   * @param {number} [options.retryCount] Maximum number of retries when connection is unsuccessful.
   * @param {number|string} [options.retryDelay] Interval between retries in seconds.
   * @returns {Configuration} The same Configuration object.
   */
  connect(target, options) {}

  /**
   * Appends a SOCKS client to the current pipeline layout.
   *
   * @param {string} target Name of the sub-pipeline to receive encoded SOCKS streams.
   * @param {string | () => string} address Host to connection in form os 'host:port'.
   * @returns {Configuration} The same Configuration object.
   */
  connectSOCKS(target, address) {}

  /**
   * Appends a TLS client to the current pipeline layout.
   *
   * @param {string} target Name of the sub-pipeline to receive encoded TLS streams.
   * @param {Object} [options] Options including certificate, trusted and sni.
   * @param {OptionCert | () => OptionCert} [options.certificate] Client certificate and private key.
   * @param {Certificate[]} [options.trusted] List of trusted server certificate.
   * @param {string | () => string} [options.sni] Host name for SNI.
   * @returns {Configuration} The same Configuration object.
   */
  connectTLS(target, options) {}

  /**
   * Appends a Dubbo decoder to the current pipeline layout.
   *
   * @returns {Configuration} The same Configuration object.
   */
  decodeDubbo() {}

  /**
   * Appends an HTTP request decoder to the current pipeline layout.
   *
   * @returns {Configuration} The same Configuration object.
   */
  decodeHTTPRequest() {}

  /**
   * Appends an HTTP response decoder to the current pipeline layout.
   *
   * @param {Object} [options] Options including bodiless.
   * @param {boolean | () => boolean} [options.bodiless] Callback function that returns true for decoding a response without a body.
   * @returns {Configuration} The same Configuration object.
   */
  decodeHTTPResponse(options) {}

  /**
   * Appends an MQTT decoder to the current pipeline layout.
   *
   * @returns {Configuration} The same Configuration object.
   */
  decodeMQTT() {}

  /**
   * Appends an HTTP message decompressor to the current pipeline layout.
   *
   * @param {() => boolean} [enable] Callback function that decides if a message should be decompressed.
   * @returns {Configuration} The same Configuration object.
   */
  decompressHTTP(enable) {}

  /**
   * Appends a generic message decompressor to the current pipeline layout.
   *
   * @param {string | () => string} algorithm Decompression algorithm to use.
   * @returns {Configuration} The same Configuration object.
   */
  decompressMessage(algorithm) {}

  /**
   * Appends a demux filter to the current pipeline layout.
   *
   * A demux filter creates a dedicated sub-pipeline for each message from the input stream,
   * collects output messages from those sub-pipelines and queues them up into the output stream.
   *
   * @param {string} layout Name of the pipeline layout based on which sub-pipelines are created to handle input Messages.
   * @returns {Configuration} The same Configuration object.
   */
  demux(layout) {}

  /**
   * Appends a demuxHTTP filter to the current pipeline layout.
   *
   * A demuxHTTP filter de-multiplexes and decodes HTTP requests from its input Data stream,
   * distribute them to many sub-pipelines and encodes and multiplexes their HTTP responses into a single output Data stream.
   * Its input and output are Data (usually TCP streams). Its sub-pipelines's input and output are HTTP Messages.
   *
   * @param {string} layout Name of the pipeline layout based on which sub-pipelines are created to handle HTTP requests.
   * @param {Object} [options] Options including bufferSize.
   * @param {number|string} [options.bufferSize] Response bodies larger than this would be transfered in chunks.
   * @returns {Configuration} The same Configuration object.
   */
  demuxHTTP(layout, options) {}

  /**
   * Appends a filter to the current pipeline layout that calls back script
   * with a detected protocol name.
   *
   * @param {(protocol) => void} handler Callback function that receives the protocol name.
   * @returns {Configuration} The same Configuration object.
   */
  detectProtocol(handler) {}

  /**
   * Appends a dummy filter to the current pipeline layout.
   *
   * @returns {Configuration} The same Configuration object.
   */
  dummy() {}

  /**
   * Appends a filter to the current pipeline layout that
   * dumps all passing events to the standard output.
   *
   * @param {string | () => string} tag Tag to print prior to the dumped messages.
   * @returns {Configuration} The same Configuration object.
   */
  dump(tag) {}

  /**
   * Appends a Dubbo encoder to the current pipeline layout.
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
   * Appends an HTTP request encoder to the current pipeline layout.
   *
   * @param {Object} [options] Options including bufferSize.
   * @param {number|string} [options.bufferSize=4096] Maximum size for a message not being encoded in chunks.
   * @returns {Configuration} The same Configuration object.
   */
  encodeHTTPRequest(options) {}

  /**
   * Appends an HTTP response encoder to the current pipeline layout.
   *
   * @param {Object} [options] Options including final, bodiless and bufferSize.
   * @param {boolean | () => boolean} [final] Decides if the message should be followed by a StreamEnd event.
   * @param {boolean | () => boolean} [bodiless] Decides if the message should have a body.
   * @param {number|string} [options.bufferSize=4096] Maximum size for a message not being encoded in chunks.
   * @returns {Configuration} The same Configuration object.
   */
  encodeHTTPResponse(options) {}

  /**
   * Appends an MQTT encoder to the current pipeline layout.
   *
   * @returns {Configuration} The same Configuration object.
   */
  encodeMQTT() {}

  /**
   * Appends a filter to the current pipeline layout that spawns an external process and
   * pumps the Data stream through its standard input/output.
   *
   * @param {string | () => string} command Shell command to execute.
   * @returns {Configuration} The same Configuration object.
   */
  exec(command) {}

  /**
   * Appends a fork filter to the current pipeline layout.
   *
   * A fork filter creates one or more sub-pipelines, each receiving a clone of all events from the filter's input.
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
   * Its input and output are Messages. Its sub-pipelines's input is also Messages while their output is simply discarded.
   *
   * @param {string} layout Name of the pipeline layout based on which shared sub-pipelines are created.
   * @param {*} group ID of the group of filters that share the same sub-pipeline.
   * @param {Object} [options] Options including maxIdle.
   * @param {number} [options.maxIdle] Time to wait before an idle shared sub-pipeline is closed.
   * @returns {Configuration} The same Configuration object.
   */
  merge(target, group, options) {}

  /**
   * Appends a mux filter to the current pipeline layout.
   *
   * A mux filter multiplexes input Messages into a shared sub-pipeline and de-multiplexes output Messages out of it.
   * Its input and output are _Messages_. Its sub-pipelines's input and output are also _Messages_.
   *
   * @param {string} layout Name of the pipeline layout based on which shared sub-pipelines are created.
   * @param {*} group ID of the group of filters that share the same sub-pipeline.
   * @param {Object} [options] Options including maxIdle.
   * @param {number} [options.maxIdle] Time to wait before an unused sub-pipeline is destroyed.
   * @returns {Configuration} The same Configuration object.
   */
  mux(layout, group, options) {}

  /**
   * Appends a muxHTTP filter to the current pipeline layout.
   *
   * A muxHTTP filter encodes and multiplexes HTTP requests into a shared sub-pipeline and de-multiplexes and decodes HTTP responses out of it.
   * Its input and output are HTTP Messages. Its sub-pipelines's input and output are Data (usually TCP streams).
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
   * Appends a filter to the current pipeline layout that calls back script
   * for every Data event.
   *
   * @param {(evt: Data) => void} handler Callback function invoked for Data events.
   * @returns {Configuration} The same Configuration object.
   */
  handleData(handler, sizeLimit) {}

  /**
   * Appends a filter to the current pipeline layout that calls back script
   * for every whole message.
   * 
   * @param {(msg: Message) => void} handler Callback function invoked for whole messages.
   * @returns {Configuration} The same Configuration object.
   */
  handleMessage(handler, sizeLimit) {}

  /**
   * Appends a filter to the current pipeline layout that calls back script
   * for every whole message body.
   *
   * @param {(evt: Data) => void} handler Callback function invoked for whole message bodies.
   * @returns {Configuration} The same Configuration object.
   */
  handleMessageBody(handler, sizeLimit) {}

  /**
   * Appends a filter to the current pipeline layout that calls back script
   * for every MessageEnd event.
   *
   * @param {(evt: MessageEnd) => void} handler Callback function invoked for MessageEnd event.
   * @returns {Configuration} The same Configuration object.
   */
  handleMessageEnd(handler) {}

  /**
   * Appends a filter to the current pipeline layout that calls back script
   * for every MessageStart event.
   *
   * @param {(evt: MessageStart) => void} handler Callback function invoked for MessageStart events.
   * @returns {Configuration} The same Configuration object.
   */
  handleMessageStart(handler) {}

  /**
   * Appends a filter to the current pipeline layout that calls back script
   * for every StreamEnd event.
   *
   * @param {(evt: StreamEnd) => void} handler Callback function invoked for StreamStart events.
   * @returns {Configuration} The same Configuration object.
   */
  handleStreamEnd(handler) {}

  /**
   * Appends a filter to the current pipeline layout that calls back script
   * for the first event in a stream.
   *
   * @param {(evt: MessageStart|MessageEnd|Data) => void} handler Callback function invoked for the first event.
   * @returns {Configuration} The same Configuration object.
   */
  handleStreamStart(handler) {}

  /**
   * Appends a filter to the current pipeline layout that calls back script
   * for the ClientHello message in TLS handshake.
   *
   * @param {(msg: ClientHello) => void} handler Callback function invoked for the ClientHello message.
   * @returns {Configuration} The same Configuration object.
   */
  handleTLSClientHello(handler) {}

  /**
   * Appends a filter to the current pipeline layout that combines multiple messages into one.
   *
   * @param {number} [batchSize = 1] Maximum number of messages to combine into one.
   * @param {Object} [options] Options including timeout and vacancy.
   * @param {number|string} [options.timeout] Time to wait for more input messages before outputting a combined one.
   * @param {number} [options.vacancy = 0.5] Maximum allowed vacancy-to-occupancy ratio of the memory chunks used by the message body.
   * @returns {Configuration} The same Configuration object.
   */
  pack(batchSize, options) {}

  /**
   * Appends a filter to the current pipeline layout that outputs Data events to
   * the standard output.
   *
   * @returns {Configuration} The same Configuration object.
   */
  print() {}

  /**
   * Appends a filter to the current pipeline layout that changes Data events into other events.
   *
   * @param {(evt: Data) => Event|Message|Event[]|Message[]} [handler] Callback function that returns events after replacement.
   * @returns {Configuration} The same Configuration object.
   */
  replaceData(handler, sizeLimit) {}

  /**
   * Appends a filter to the current pipeline layout that changes whole messages into other events.
   *
   * @param {(msg: Message) => Event|Message|Event[]|Message[]} [handler] Callback function that returns replacement to the input.
   * @param {number|string} [sizeLimit]
   * @returns {Configuration} The same Configuration object.
   */
  replaceMessage(handler,sizeLimit) {}

  /**
   * Appends a filter to the current pipeline layout that changes whole bodies into other events.
   *
   * @param {(evt: Data) => Event|Message|Event[]|Message[]} [handler] Callback function that returns replacement to the input.
   * @param {number|string} [sizeLimit]
   * @returns {Configuration} The same Configuration object.
   */
  replaceMessageBody(handler, sizeLimit) {}

  /**
   * Appends a filter to the current pipeline layout that changes MessageEnd events into other events.
   *
   * @param {(evt: MessageEnd) => Event|Message|Event[]|Message[]} [handler] Callback function that returns replacement to the input.
   * @returns {Configuration} The same Configuration object.
   */
  replaceMessageEnd(handler) {}

  /**
   * Appends a filter to the current pipeline layout that changes MessageStart events into other events.
   *
   * @param {(evt: MessageStart) => Event|Message|Event[]|Message[]} [handler] Callback function that returns replacement to the input.
   * @returns {Configuration} The same Configuration object.
   */
  replaceMessageStart(handler) {}

  /**
   * Appends a filter to the current pipeline layout that changes StreamEnd events into other events.
   *
   * @param {(evt: StreamEnd) => Event|Message|Event[]|Message[]} [handler] Callback function that returns replacement to the input.
   * @returns {Configuration} The same Configuration object.
   */
  replaceStreamEnd(handler) {}

  /**
   * Appends a filter to the current pipeline layout that changes the first event in a stream into other events.
   *
   * @param {(evt: MessageStart|MessageEnd|Data) => Event|Message|Event[]|Message[]} [handler] Callback function that returns replacement to the input.
   * @returns {Configuration} The same Configuration object.
   */
  replaceStreamStart(handler) {}

  /**
   * Appends a serveHTTP filter to the current pipeline layout.
   *
   * A serveHTTP filter de-multiplexes and decodes HTTP requests from its input Data stream,
   * calls a user-defined script handler for each request to get a response
   * and encodes and multiplexes those HTTP responses into a single output Data stream.
   * Its input and output are Data (usually TCP streams).
   *
   * @param {Message | MessageHandler} handler A response message or a callback function that receives a request and returns a response message.
   * @returns {Configuration} The same Configuration object.
   */
  serveHTTP(handler) {}

  /**
   * Appends a filter to the current pipeline layout that cut Data events into smaller ones.
   *
   * @param {(number) => boolean} handler Callback function that gets called for every byte and decides where to split up.
   * @returns {Configuration} The same Configuration object.
   */
  split(handler) {}

  /**
   * Appends a filter to the current pipeline layout that only allows a certain
   * amount of data to go through every second.
   *
   * @param {number | string | (() => number|string)} quota Amount of data in bytes that are allowed through every second.
   * @param {any | () => any} [account] Name of the account that the quota is entitled to.
   * @returns {Configuration} The same Configuration object.
   */
  throttleDataRate(limit, account) {}

  /**
   * Appends a filter to the current pipeline layout that only allows a certain
   * number of messages to go through every second.
   *
   * @param {number | () => number)} quota Number of messages allowed through every second.
   * @param {any | () => any} [account] Name of the account that the quota is entitled to.
   * @returns {Configuration} The same Configuration object.
   */
  throttleMessageRate(limit, account) {}

  /**
   * Appends a ***use*** filter to the current pipeline layout.
   *
   * A ***use*** filter pumps events through a sub-pipeline in a different module, or a series of sub-pipelines in a module chain.
   *
   * @param {string|string[]} filenames One or more module filenames.
   * @param {string} layout Name of the pipeline layout to process input events in all modules.
   * @param {string} [layoutDown] Name of the pipeline layout to process output events in all modules.
   * @param {BooleanCB} [turnDown] Callback function that decides where the chaining should turn back.
   * @returns {Configuration} The same Configuration object.
   */
  use(filenames, layout, layoutDown, turnDown) {}

  /**
   * Appends a filter to the current pipeline layout that blocks all events
   * up until a certain condition is met.
   *
   * @param {() => bool} [condition] Callback function that returns true to unblock events.
   * @returns {Configuration} The same Configuration object.
   */
  wait(condition) {}
}
