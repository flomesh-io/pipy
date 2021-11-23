/**
 * Utility class for defining Pipy pipelines.
 *
 */
class Configuration {

  /**
   * @param {string} ns 
   * @param {Object} variables 
   * @returns {Configuration} The same Configuration object.
   */
  export(ns, variables) {}

  /**
   * @param {Object.<string, string>} variabls
   * @returns {Configuration} The same Configuration object.
   */
  import(variabls) {}

  /**
   * Defines a listening port.
   *
   * @param {number|string} port Port to listen on.
   * @returns {Configuration} The same Configuration object.
   */
  listen(port) {}

  /**
   * @param {number|string} [interval] 
   * @returns {Configuration} The same Configuration object.
   */
  task(interval) {}

  /**
   * @param {string} name 
   * @returns {Configuration} The same Configuration object.
   */
  pipeline(name) {}

  /**
   * @param {string} target
   * @param {(address: string, port: number) => boolean} onConnect
   * @returns {Configuration} The same Configuration object.
   */
  acceptSOCKS(target, onConnect) {}

  /**
   * @param {string} target
   * @param {Object} options
   * @returns {Configuration} The same Configuration object.
   */
  acceptTLS(target, options) {}

  /**
   * @param {string | () => string} target
   * @param {Object} [options]
   * @param {number|string} [options.bufferLimit]
   * @param {number} [options.retryCount]
   * @param {number|string} [options.retryDelay]
   * @returns {Configuration} The same Configuration object.
   */
  connect(target, options) {}

  /**
   * @param {string} target
   * @param {string | () => string} address
   * @returns {Configuration} The same Configuration object.
   */
   connectSOCKS(target, address) {}

  /**
   * @param {string} target
   * @param {Object} [options]
   * @param {Certificate|() => Certificate} [options.certificate]
   * @param {Certificate[]} [options.trusted]
   * @param {string | () => string} [options.sni]
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
   * @param {Object} [options]
   * @param {boolean | () => boolean} [options.bodiless]
   * @returns {Configuration} The same Configuration object.
   */
  decodeHTTPResponse(options) {}

  /**
   * @param {() => boolean} [enable]
   * @returns {Configuration} The same Configuration object.
   */
  decompressHTTP(enable) {}

  /**
   * @param {string | () => string} algorithm
   * @returns {Configuration} The same Configuration object.
   */
  decompressMessage(algorithm) {}

  /**
   * @param {string} target
   * @returns {Configuration} The same Configuration object.
   */
  demux(target) {}

  /**
   * @param {string} target
   * @returns {Configuration} The same Configuration object.
   */
  demuxHTTP(target, options) {}

  /**
   * @returns {Configuration} The same Configuration object.
   */
  dummy() {}

  /**
   * @param {string | () => string} tag
   * @returns {Configuration} The same Configuration object.
   */
  dump(tag) {}

  /**
   * @param {Object | () => Object} [head]
   * @param {number|string} [head.id]
   * @param {number} [head.status]
   * @param {boolean} [head.isRequest]
   * @param {boolean} [head.isTwoWay]
   * @param {boolean} [head.isEvent]
   * @returns {Configuration} The same Configuration object.
   */
  encodeDubbo(head) {}

  /**
   * @returns {Configuration} The same Configuration object.
   */
  encodeHTTPRequest() {}

  /**
   * @param {Object} [options]
   * @param {boolean | () => boolean} [options.bodiless]
   * @returns {Configuration} The same Configuration object.
   */
  encodeHTTPResponse(options) {}

  /**
   * @param {string | () => string} command
   * @returns {Configuration} The same Configuration object.
   */
  exec(command) {}

  /**
   * @param {string} target
   * @param {Object | Object[] | (() => Object|Object[])} [variables]
   * @returns {Configuration} The same Configuration object.
   */
  fork(target, variables) {}

  /**
   * @param {string} target
   * @param {() => boolean} [condition]
   * @param {...(string | () => boolean)} rest
   * @returns {Configuration} The same Configuration object.
   */
  link(target, condition, ...rest) {}

  /**
   * @param {string} target
   * @param {any | () => any} key
   * @param {Object} [options]
   * @param {number} [options.maxIdle]
   * @returns {Configuration} The same Configuration object.
   */
  merge(target, key, options) {}

  /**
   * @param {string} target
   * @param {any | () => any} key
   * @param {Object} [options]
   * @param {number} [options.maxIdle]
   * @returns {Configuration} The same Configuration object.
   */
  mux(target, key, options) {}

  /**
   * @param {string} target
   * @param {any | () => any} key
   * @param {Object} [options]
   * @param {number} [options.maxIdle]
   * @returns {Configuration} The same Configuration object.
   */
  muxHTTP(target, key, options) {}

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
   * @param {(evt: StreamEnd) => void} handler
   * @returns {Configuration} The same Configuration object.
   */
  handleStreamEnd(handler) {}

  /**
   * @param {(evt: MessageStart|MessageEnd|Data) => void} handler
   * @returns {Configuration} The same Configuration object.
   */
  handleStreamStart(handler) {}

  /**
   * @param {number} [batchSize = 1]
   * @param {Object} [options]
   * @param {number|string} [options.timeout]
   * @param {number} [options.vacancy = 0.5]
   * @returns {Configuration} The same Configuration object.
   */
  pack(batchSize, options) {}

  /**
   * @returns {Configuration} The same Configuration object.
   */
  print() {}

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
   * @param {(evt: StreamEnd) => Event|Message|Event[]|Message[]} [handler]
   * @returns {Configuration} The same Configuration object.
   */
  replaceStreamEnd(handler) {}

  /**
   * @param {(evt: MessageStart|MessageEnd|Data) => Event|Message|Event[]|Message[]} [handler]
   * @returns {Configuration} The same Configuration object.
   */
  replaceStreamStart(handler) {}

  /**
   * Handles HTTP requests in a stream.
   *
   * @param {(msg : Message) => Message} handler Callback function that receives a request and returns a response.
   * @returns {Configuration} The same Configuration object.
   */
  serveHTTP(handler) {}

  /**
   * @param {(number) => boolean} handler
   * @returns {Configuration} The same Configuration object.
   */
  split(handler) {}

  /**
   * @param {number | string | (() => number|string)} limit
   * @param {any | () => any} [account]
   * @returns {Configuration} The same Configuration object.
   */
  throttleDataRate(limit, account) {}

  /**
   * @param {number | () => number)} limit
   * @param {any | () => any} [account]
   * @returns {Configuration} The same Configuration object.
   */
  throttleMessageRate(limit, account) {}

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
