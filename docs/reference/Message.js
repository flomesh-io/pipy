/**
 * Wrapper class combining MessageStart, Data and MessageEnd events.
 */
class Message {

  /**
   * Constructs a Message with head and body.
   *
   * @param {Object} [head] Message head.
   * @param {Data} body Message body.
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
