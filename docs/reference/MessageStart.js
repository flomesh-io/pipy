/**
 * MessageStart marks the start of a message in an event stream.
 * It also contains header (or meta) information for that message in its head property.
 */
class MessageStart {

  /**
   * Creates an instance of MessageStart.
   *
   * @param {Object} [head] Message head.
   */
  constructor(head) {}

  /**
   * Message head.
   *
   * @type {Object}
   * @readyonly
   */
  head = null;
}
