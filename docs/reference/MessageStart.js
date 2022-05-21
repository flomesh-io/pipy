/**
 * MessageStart marks the start of a message in an event stream.
 * It also contains meta-info of that message in its optional head property.
 */
class MessageStart {

  /**
   * Creates an instance of MessageStart.
   *
   * @param {Object} [head] Message meta-info in the head.
   */
  constructor(head) {}

  /**
   * Message meta-info in the head.
   *
   * @type {Object}
   * @readyonly
   */
  head = null;
}
