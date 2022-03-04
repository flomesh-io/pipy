/**
 * Message is a container of a series of events that compose a whole message in an event stream.
 */
class Message {

  /**
   * Creates an instance of Message.
   *
   * @param {Object} [head] Message head.
   * @param {string|Data} body Message body.
   */
  constructor(head, body) {}

  /**
   * Message head.
   *
   * @type {Object}
   * @readyonly
   */
  head = null;

  /**
   * Message body.
   *
   * @type {Data}
   * @readyonly
   */
  body = null; 
}
