/**
 * Utility class for defining Pipy pipelines.
 *
 */
class Configuration {

  /**
   * Defines a listening port.
   *
   * @param {number|string} port Port to listen on.
   * @returns {Configuration} The same Configuration object.
   */
  listen(port) {}

  /**
   * @param {string} target
   * @param {Object} options
   * @returns {Configuration} The same Configuration object.
   */
  acceptTLS(target, options) {}

  /**
   * @param {string | () => string} target
   * @param {Object} [options]
   * @returns {Configuration} The same Configuration object.
   */
  connect(target, options) {}

  /**
   * @returns {Configuration} The same Configuration object.
   */
  connectTLS(target, options) {}

  /**
   * @returns {Configuration} The same Configuration object.
   */
  decodeDubbo() {}

  /**
   * @returns {Configuration} The same Configuration object.
   */
  decodeHTTPRequest() {}

  /**
   * @returns {Configuration} The same Configuration object.
   */
  decodeHTTPResponse(options) {}

  /**
   * @returns {Configuration} The same Configuration object.
   */
  decompressHTTP(enable) {}

  /**
   * @returns {Configuration} The same Configuration object.
   */
  decompressMessage(algorithm) {}

  /**
   * @returns {Configuration} The same Configuration object.
   */
  demux(target) {}

  /**
   * @param {string} target
   * @param {Object} [options]
   * @returns {Configuration} The same Configuration object.
   */
  demuxHTTP(target, options) {}

  /**
   * @returns {Configuration} The same Configuration object.
   */
  dummy() {}

  /**
   * @returns {Configuration} The same Configuration object.
   */
  dump(tag) {}

  /**
   * @returns {Configuration} The same Configuration object.
   */
  encodeDubbo(head) {}

  /**
   * @returns {Configuration} The same Configuration object.
   */
  encodeHTTPRequest() {}

  /**
   * @returns {Configuration} The same Configuration object.
   */
  encodeHTTPResponse(options) {}

  /**
   * @returns {Configuration} The same Configuration object.
   */
  exec(command) {}

  /**
   * @returns {Configuration} The same Configuration object.
   */
  fork(target, initializers) {}

  /**
   * @returns {Configuration} The same Configuration object.
   */
  link(target, condition, ...more) {}

  /**
   * @returns {Configuration} The same Configuration object.
   */
  merge(target, stream) {}

  /**
   * @param {string} target
   * @param {string | () => string} [stream]
   * @returns {Configuration} The same Configuration object.
   */
  mux(target, stream) {}

  /**
   * @param {string} target
   * @param {string | () => string} [stream]
   * @returns {Configuration} The same Configuration object.
   */
  muxHTTP(target, stream) {}

  /**
   * @param {(evt: Data) => void} handler
   * @param {number|string} [sizeLimit]
   * @returns {Configuration} The same Configuration object.
   */
  handleData(handler, sizeLimit) {}

  /**
   * @param {(msg: Message) => void} handler
   * @param {number|string} [sizeLimit]
   * @returns {Configuration} The same Configuration object.
   */
  handleMessage(handler, sizeLimit) {}

  /**
   * @param {number|string} [sizeLimit]
   * @param {(evt: Data) => void} handler
   * @returns {Configuration} The same Configuration object.
   */
  handleMessageBody(handler, sizeLimit) {}

  /**
   * @param {(evt: MessageEnd) => void} handler
   * @returns {Configuration} The same Configuration object.
   */
  handleMessageEnd(handler) {}

  /**
   * @param {(evt: MessageStart) => void} handler
   * @returns {Configuration} The same Configuration object.
   */
  handleMessageStart(handler) {}

  /**
   * @param {(evt: SessionEnd) => void} handler
   * @returns {Configuration} The same Configuration object.
   */
  handleSessionEnd(handler) {}

  /**
   * @param {(evt: MessageStart|MessageEnd|Data) => void} handler
   * @returns {Configuration} The same Configuration object.
   */
  handleSessionStart(handler) {}

  /**
   * @returns {Configuration} The same Configuration object.
   */
  pack(batchSize, options) {}

  /**
   * @returns {Configuration} The same Configuration object.
   */
  print() {}

  /**
   * @returns {Configuration} The same Configuration object.
   */
  proxySOCKS(target, onConnect) {}

  /**
   * @param {(evt: Data) => Event|Message|Event[]|Message[]} [handler]
   * @param {number|string} [sizeLimit]
   * @returns {Configuration} The same Configuration object.
   */
  replaceData(handler, sizeLimit) {}

  /**
   * @param {(msg: Message) => Event|Message|Event[]|Message[]} [handler]
   * @param {number|string} [sizeLimit]
   * @returns {Configuration} The same Configuration object.
   */
  replaceMessage(handler,sizeLimit) {}

  /**
   * @param {(evt: Data) => Event|Message|Event[]|Message[]} [handler]
   * @param {number|string} [sizeLimit]
   * @returns {Configuration} The same Configuration object.
   */
  replaceMessageBody(handler, sizeLimit) {}

  /**
   * @param {(evt: MessageEnd) => Event|Message|Event[]|Message[]} [handler]
   * @returns {Configuration} The same Configuration object.
   */
  replaceMessageEnd(handler) {}

  /**
   * @param {(evt: MessageStart) => Event|Message|Event[]|Message[]} [handler]
   * @returns {Configuration} The same Configuration object.
   */
  replaceMessageStart(handler) {}

  /**
   * @param {(evt: SessionEnd) => Event|Message|Event[]|Message[]} [handler]
   * @returns {Configuration} The same Configuration object.
   */
  replaceSessionEnd(handler) {}

  /**
   * @param {(evt: MessageStart|MessageEnd|Data) => Event|Message|Event[]|Message[]} [handler]
   * @returns {Configuration} The same Configuration object.
   */
  replaceSessionStart(handler) {}

  /**
   * Handles HTTP requests in a stream.
   *
   * @param {(msg : Message) => Message} handler Callback function that receives a request and returns a response.
   * @returns {Configuration} The same Configuration object.
   */
  serveHTTP(handler) {}

  /**
   * @returns {Configuration} The same Configuration object.
   */
  split(handler) {}

  /**
   * @returns {Configuration} The same Configuration object.
   */
  tap(quota, account) {}

  /**
   * @param {string|string[]} modules
   * @param {string} pipeline
   * @param {string} [pipelineDown]
   * @param {() => bool} [turnDown]
   * @returns {Configuration} The same Configuration object.
   */
  use(modules, pipeline, pipelineDown, turnDown) {}

  /**
   * @param {() => bool} [condition]
   * @returns {Configuration} The same Configuration object.
   */
  wait(condition) {}
}
