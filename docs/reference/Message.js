/**
 * Message is a container of a series of events that compose a whole message in an event stream.
 */
class Message {

  /**
   * Creates an instance of Message.
   *
   * @param {Object} [head] Message meta-info in the head.
   * @param {string|Data} body Message content.
   * @param {Object} [tail] Message meta-info in the tail.
   */
  constructor(head, body, tail) {}

  /**
   * Message meta-info in the head.
   *
   * @type {Object}
   * @readyonly
   */
  head = null;

  /**
   * Message meta-info in the tail.
   *
   * @type {Object}
   * @readyonly
   */
  tail = null;

  /**
   * Message content.
   *
   * @type {Data}
   * @readyonly
   */
  body = null; 
}
