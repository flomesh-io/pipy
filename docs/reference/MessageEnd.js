/**
 * MessageEnd marks the end of a message in an event stream.
 * It also contains meta-info of that message in its optional tail property.
 */
class MessageEnd {

  /**
   * Creates an instance of MessageEnd.
   *
   * @param {Object} [tail] Message meta-info in the tail.
   */
  constructor(tail) {}

  /**
   * Message meta-info in the tail.
   *
   * @type {Object}
   * @readyonly
   */
  tail = null;
}
