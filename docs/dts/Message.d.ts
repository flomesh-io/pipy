/**
 * Container of a series of events that compose a whole message in an event stream.
 */

declare class Message {

  /**
   * Creates a _Message_ object.
   */
  constructor(body?: string | Data);
  constructor(head: object, body?: string | Data, tail?: object);

  /**
   * Protocol-dependent message header.
   */
  head?: object;

  /**
   * Protocol-dependent message footer.
   */
  tail?: object;

  /**
   * Message body or payload.
   */
  body?: Data;
}
