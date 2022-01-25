/**
 * Utility class for building Pipy pipelines.
 */
class Configuration {

  /**
   * @typedef {{ key: PrivateKey, cert: Certificate|CertificateChain }} OptionCert
   * @typedef {{ serverNames: string[], protocolNames: string[] }} ClientHello
   */

  /**
   * Defines global variables that can be imported by other modules.
   *
   * @param {string} ns Namespace where the variables are imported from.
   * @param {Object} variables Variable names and their initial values.
   * @returns {Configuration} The same Configuration object.
   */
  export(ns, variables) {}

  /**
   * Imports global variables from other modules.
   *
   * @param {Object.<string, string>} variables Variable names and their namespaces.
   * @returns {Configuration} The same Configuration object.
   */
  import(variabls) {}

  /**
   * Starts a pipeline listening on a TCP port.
   *
   * @param {number|string} port Port or [ip]:port pair to listen on.
   * @param {Object} [options] Options including maxConnections, readTimeout and writeTimeout.
   * @param {number} [options.maxConnections] Maximum number of concurrent connections.
   * @param {number|string} [options.readTimeout] Read timeout in seconds.
   * @param {number|string} [options.writeTimeout] Write timeout in seconds.
   * @returns {Configuration} The same Configuration object.
   */
  listen(port) {}

  /**
   * Starts a pipeline that executes a cron task.
   *
   * @param {number|string} [interval] Interval between recurrences in seconds.
   * @returns {Configuration} The same Configuration object.
   */
  task(interval) {}

  /**
   * Starts a sub-pipeline by a given name.
   *
   * @param {string} name The name of the sub-pipeline.
   * @returns {Configuration} The same Configuration object.
   */
  pipeline(name) {}

  /**
   * Appends a SOCKS server to the current pipeline.
   *
   * @param {string} target Name of the sub-pipeline to receive decoded SOCKS streams.
   * @param {(address: string, port: number) => boolean} onConnect Callback function when a client tries to connect.
   * @returns {Configuration} The same Configuration object.
   */
  acceptSOCKS(target, onConnect) {}

  /**
   * Appends a TLS server to the current pipeline.
   *
   * @param {string} target Name of the sub-pipeline to receive decoded TLS streams.
   * @param {Object} [options] Options including certificate and trusted.
   * @param {OptionCert | () => OptionCert} [options.certificate] Server certificate and private key.
   * @param {Certificate[]} [options.trusted] List of trusted client certifcates.
   * @returns {Configuration} The same Configuration object.
   */
  acceptTLS(target, options) {}

  /**
   * Appends a TCP client to the current pipeline.
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
   * Appends a SOCKS client to the current pipeline.
   *
   * @param {string} target Name of the sub-pipeline to receive encoded SOCKS streams.
   * @param {string | () => string} address Host to connection in form os 'host:port'.
   * @returns {Configuration} The same Configuration object.
   */
  connectSOCKS(target, address) {}

  /**
   * Appends a TLS client to the current pipeline.
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
   * Appends a Dubbo decoder to the current pipeline.
   *
   * @returns {Configuration} The same Configuration object.
   */
  decodeDubbo() {}

  /**
   * Appends an HTTP request decoder to the current pipeline.
   *
   * @returns {Configuration} The same Configuration object.
   */
  decodeHTTPRequest() {}

  /**
   * Appends an HTTP response decoder to the current pipeline.
   *
   * @param {Object} [options] Options including bodiless.
   * @param {boolean | () => boolean} [options.bodiless] Callback function that returns true for decoding a response without a body.
   * @returns {Configuration} The same Configuration object.
   */
  decodeHTTPResponse(options) {}

  /**
   * Appends an MQTT decoder to the current pipeline.
   *
   * @returns {Configuration} The same Configuration object.
   */
  decodeMQTT() {}

  /**
   * Appends an HTTP message decompressor to the current pipeline.
   *
   * @param {() => boolean} [enable] Callback function that decides if a message should be decompressed.
   * @returns {Configuration} The same Configuration object.
   */
  decompressHTTP(enable) {}

  /**
   * Appends a generic message decompressor to the current pipeline.
   *
   * @param {string | () => string} algorithm Decompression algorithm to use.
   * @returns {Configuration} The same Configuration object.
   */
  decompressMessage(algorithm) {}

  /**
   * Appends a generic message demuxer to the current pipeline that
   * pumps each individual message through a separate sub-pipeline.
   *
   * @param {string} target Name of the sub-pipelines to pump messages through.
   * @returns {Configuration} The same Configuration object.
   */
  demux(target) {}

  /**
   * Appends an HTTP demuxer to the current pipeline that
   * decodes HTTP messages from a multiplexed Data stream and
   * pumps each of them through a separate sub-pipeline.
   *
   * @param {string} target Name of the sub-pipelines to pump messages through.
   * @returns {Configuration} The same Configuration object.
   */
  demuxHTTP(target, options) {}

  /**
   * Appends a filter to the current pipeline that calls back script
   * with a detected protocol name.
   *
   * @param {(protocol) => void} handler Callback function that receives the protocol name.
   * @returns {Configuration} The same Configuration object.
   */
  detectProtocol(handler) {}

  /**
   * Appends a dummy filter to the current pipeline.
   *
   * @returns {Configuration} The same Configuration object.
   */
  dummy() {}

  /**
   * Appends a filter to the current pipeline that
   * dumps all passing events to the standard output.
   *
   * @param {string | () => string} tag Tag to print prior to the dumped messages.
   * @returns {Configuration} The same Configuration object.
   */
  dump(tag) {}

  /**
   * Appends a Dubbo encoder to the current pipeline.
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
   * Appends an HTTP request encoder to the current pipeline.
   *
   * @param {Object} [options] Options including bufferSize.
   * @param {number|string} [options.bufferSize=4096] Maximum size for a message not being encoded in chunks.
   * @returns {Configuration} The same Configuration object.
   */
  encodeHTTPRequest(options) {}

  /**
   * Appends an HTTP response encoder to the current pipeline.
   *
   * @param {Object} [options] Options including final, bodiless and bufferSize.
   * @param {boolean | () => boolean} [final] Decides if the message should be followed by a StreamEnd event.
   * @param {boolean | () => boolean} [bodiless] Decides if the message should have a body.
   * @param {number|string} [options.bufferSize=4096] Maximum size for a message not being encoded in chunks.
   * @returns {Configuration} The same Configuration object.
   */
  encodeHTTPResponse(options) {}

  /**
   * Appends an MQTT encoder to the current pipeline.
   *
   * @returns {Configuration} The same Configuration object.
   */
  encodeMQTT() {}

  /**
   * Appends a filter to the current pipeline that spawns an external process and
   * pumps the Data stream through its standard input/output.
   *
   * @param {string | () => string} command Shell command to execute.
   * @returns {Configuration} The same Configuration object.
   */
  exec(command) {}

  /**
   * Appends a filter to the current pipeline that starts one or more sub-pipelines
   * and clones all events to them.
   *
   * @param {string} target Name of the sub-pipelines that receive clones of events.
   * @param {Object | Object[] | (() => Object|Object[])} [variables] Initial global variables of each sub-pipeline.
   * @returns {Configuration} The same Configuration object.
   */
  fork(target, variables) {}

  /**
   * Appends a filter to the current pipeline that picks a sub-pipeline
   * out of a few and pumps all events through it.
   *
   * @param {string} target Name of the sub-pipeline where events are pumped through.
   * @param {() => boolean} [condition] Condition under which the last sub-pipeline is picked.
   * @param {...(string | () => boolean)} rest Other target/condition pairs to choose from.
   * @returns {Configuration} The same Configuration object.
   */
  link(target, condition, ...rest) {}

  /**
   * Appends a filter to the current pipeline that
   * clones and queues up all messages to a shared sub-pipeline.
   *
   * @param {string} target Name of the sub-pipeline that receive clones of messages.
   * @param {any | () => any} key ID by which a sub-pipeline is specified to share.
   * @param {Object} [options] Options including maxIdle.
   * @param {number} [options.maxIdle] Time to wait before an idle shared sub-pipeline is closed.
   * @returns {Configuration} The same Configuration object.
   */
  merge(target, key, options) {}

  /**
   * Appends a generic message muxer to the current pipeline that
   * pumps all messages through a shared sub-pipeline.
   *
   * @param {string} target Name of the sub-pipeline where messages are pumped through.
   * @param {any | () => any} key ID by which a sub-pipeline is specified to share.
   * @param {Object} [options] Options including maxIdle.
   * @param {number} [options.maxIdle] Time to wait before an idle shared sub-pipeline is closed.
   * @returns {Configuration} The same Configuration object.
   */
  mux(target, key, options) {}

  /**
   * Appends an HTTP muxer to the current pipeline that
   * encodes HTTP messages into a multiplexed Data stream and
   * pumps it through a shared sub-pipeline.
   *
   * @param {string} target Name of the sub-pipeline where messages are pumped through.
   * @param {any | () => any} key ID by which a sub-pipeline is specified to share.
   * @param {Object} [options] Options including maxIdle.
   * @param {number} [options.maxIdle] Time to wait before an idle shared sub-pipeline is closed.
   * @returns {Configuration} The same Configuration object.
   */
  muxHTTP(target, key, options) {}

  /**
   * Appends a filter to the current pipeline that calls back script
   * for every Data event.
   *
   * @param {(evt: Data) => void} handler Callback function invoked for Data events.
   * @returns {Configuration} The same Configuration object.
   */
  handleData(handler, sizeLimit) {}

  /**
   * Appends a filter to the current pipeline that calls back script
   * for every whole message.
   * 
   * @param {(msg: Message) => void} handler Callback function invoked for whole messages.
   * @returns {Configuration} The same Configuration object.
   */
  handleMessage(handler, sizeLimit) {}

  /**
   * Appends a filter to the current pipeline that calls back script
   * for every whole message body.
   *
   * @param {(evt: Data) => void} handler Callback function invoked for whole message bodies.
   * @returns {Configuration} The same Configuration object.
   */
  handleMessageBody(handler, sizeLimit) {}

  /**
   * Appends a filter to the current pipeline that calls back script
   * for every MessageEnd event.
   *
   * @param {(evt: MessageEnd) => void} handler Callback function invoked for MessageEnd event.
   * @returns {Configuration} The same Configuration object.
   */
  handleMessageEnd(handler) {}

  /**
   * Appends a filter to the current pipeline that calls back script
   * for every MessageStart event.
   *
   * @param {(evt: MessageStart) => void} handler Callback function invoked for MessageStart events.
   * @returns {Configuration} The same Configuration object.
   */
  handleMessageStart(handler) {}

  /**
   * Appends a filter to the current pipeline that calls back script
   * for every StreamEnd event.
   *
   * @param {(evt: StreamEnd) => void} handler Callback function invoked for StreamStart events.
   * @returns {Configuration} The same Configuration object.
   */
  handleStreamEnd(handler) {}

  /**
   * Appends a filter to the current pipeline that calls back script
   * for the first event in a stream.
   *
   * @param {(evt: MessageStart|MessageEnd|Data) => void} handler Callback function invoked for the first event.
   * @returns {Configuration} The same Configuration object.
   */
  handleStreamStart(handler) {}

  /**
   * Appends a filter to the current pipeline that calls back script
   * for the ClientHello message in TLS handshake.
   *
   * @param {(msg: ClientHello) => void} handler Callback function invoked for the ClientHello message.
   * @returns {Configuration} The same Configuration object.
   */
  handleTLSClientHello(handler) {}

  /**
   * Appends a filter to the current pipeline that combines multiple messages into one.
   *
   * @param {number} [batchSize = 1] Maximum number of messages to combine into one.
   * @param {Object} [options] Options including timeout and vacancy.
   * @param {number|string} [options.timeout] Time to wait for more input messages before outputting a combined one.
   * @param {number} [options.vacancy = 0.5] Maximum allowed vacancy-to-occupancy ratio of the memory chunks used by the message body.
   * @returns {Configuration} The same Configuration object.
   */
  pack(batchSize, options) {}

  /**
   * Appends a filter to the current pipeline that outputs Data events to
   * the standard output.
   *
   * @returns {Configuration} The same Configuration object.
   */
  print() {}

  /**
   * Appends a filter to the current pipeline that changes Data events into other events.
   *
   * @param {(evt: Data) => Event|Message|Event[]|Message[]} [handler] Callback function that returns events after replacement.
   * @returns {Configuration} The same Configuration object.
   */
  replaceData(handler, sizeLimit) {}

  /**
   * Appends a filter to the current pipeline that changes whole messages into other events.
   *
   * @param {(msg: Message) => Event|Message|Event[]|Message[]} [handler] Callback function that returns replacement to the input.
   * @param {number|string} [sizeLimit]
   * @returns {Configuration} The same Configuration object.
   */
  replaceMessage(handler,sizeLimit) {}

  /**
   * Appends a filter to the current pipeline that changes whole bodies into other events.
   *
   * @param {(evt: Data) => Event|Message|Event[]|Message[]} [handler] Callback function that returns replacement to the input.
   * @param {number|string} [sizeLimit]
   * @returns {Configuration} The same Configuration object.
   */
  replaceMessageBody(handler, sizeLimit) {}

  /**
   * Appends a filter to the current pipeline that changes MessageEnd events into other events.
   *
   * @param {(evt: MessageEnd) => Event|Message|Event[]|Message[]} [handler] Callback function that returns replacement to the input.
   * @returns {Configuration} The same Configuration object.
   */
  replaceMessageEnd(handler) {}

  /**
   * Appends a filter to the current pipeline that changes MessageStart events into other events.
   *
   * @param {(evt: MessageStart) => Event|Message|Event[]|Message[]} [handler] Callback function that returns replacement to the input.
   * @returns {Configuration} The same Configuration object.
   */
  replaceMessageStart(handler) {}

  /**
   * Appends a filter to the current pipeline that changes StreamEnd events into other events.
   *
   * @param {(evt: StreamEnd) => Event|Message|Event[]|Message[]} [handler] Callback function that returns replacement to the input.
   * @returns {Configuration} The same Configuration object.
   */
  replaceStreamEnd(handler) {}

  /**
   * Appends a filter to the current pipeline that changes the first event in a stream into other events.
   *
   * @param {(evt: MessageStart|MessageEnd|Data) => Event|Message|Event[]|Message[]} [handler] Callback function that returns replacement to the input.
   * @returns {Configuration} The same Configuration object.
   */
  replaceStreamStart(handler) {}

  /**
   * Appends an HTTP server to the current pipeline.
   *
   * @param {(msg : Message) => Message} handler Callback function that receives a request and returns a response.
   * @returns {Configuration} The same Configuration object.
   */
  serveHTTP(handler) {}

  /**
   * Appends a filter to the current pipeline that cut Data events into smaller ones.
   *
   * @param {(number) => boolean} handler Callback function that gets called for every byte and decides where to split up.
   * @returns {Configuration} The same Configuration object.
   */
  split(handler) {}

  /**
   * Appends a filter to the current pipeline that only allows a certain
   * amount of data to go through every second.
   *
   * @param {number | string | (() => number|string)} quota Amount of data in bytes that are allowed through every second.
   * @param {any | () => any} [account] Name of the account that the quota is entitled to.
   * @returns {Configuration} The same Configuration object.
   */
  throttleDataRate(limit, account) {}

  /**
   * Appends a filter to the current pipeline that only allows a certain
   * number of messages to go through every second.
   *
   * @param {number | () => number)} quota Number of messages allowed through every second.
   * @param {any | () => any} [account] Name of the account that the quota is entitled to.
   * @returns {Configuration} The same Configuration object.
   */
  throttleMessageRate(limit, account) {}

  /**
   * Appends a filter to the current pipeline that pumps events through
   * sub-pipelines in a module chain.
   *
   * @param {string|string[]} modules One or more module filenames.
   * @param {string} pipeline Name of the sub-pipelines to receive events in each module on the chain.
   * @param {string} [pipelineDown] Name of the sub-pipelines to process returning events in each module.
   * @param {() => bool} [turnDown] Callback function that decides where the chaining should stop and go back.
   * @returns {Configuration} The same Configuration object.
   */
  use(modules, pipeline, pipelineDown, turnDown) {}

  /**
   * Appends a filter to the current pipeline that blocks all events
   * up until a certain condition is met.
   *
   * @param {() => bool} [condition] Callback function that returns true to unblock events.
   * @returns {Configuration} The same Configuration object.
   */
  wait(condition) {}
}
