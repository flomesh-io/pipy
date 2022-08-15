/**
 * Event to mark the end of a message in an event stream.
 */
declare class MessageEnd extends Event {

  /**
   * Creates an instance of _MessageEnd_ object.
   */
  constructor(tail?: object);

  /**
   * Protocol-dependent message footer.
   */
  tail?: object;
}
