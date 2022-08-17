/**
 * Container of a series of _Events_ that compose a whole message.
 */
declare class Message {

  /**
   * Creates an instance of _Message_.
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
