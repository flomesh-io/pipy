/**
 * Event to mark the start of a message in an event stream.
 */
declare class MessageStart extends Event {

  /**
   * Creates an instance of _MessageStart_ object.
   */
  constructor(head?: object);

  /**
   * Protocol-dependent message header.
   */
  head?: object;
}
