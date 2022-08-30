/// <reference path="./Event.d.ts" />

/**
 * Container of a series of _Events_ that compose a whole message.
 */
interface Message {

  /**
   * Protocol-dependent message header.
   */
  head?: object;

  /**
   * Protocol-dependent message footer.
   */
  tail?: object;

  /**
   * Message body or payload as a _Data_ object.
   */
  body?: Data;
}

interface MessageConstructor {

  /**
   * Creates an instance of _Message_.
   *
   * @param body A string or a _Data_ object for the message body.
   * @returns A _Message_ object containing the specified body.
   */
  new(body?: string | Data): Message;

  /**
   * Creates an instance of _Message_.
   *
   * @param head An object containing protocol-dependent message header.
   * @param body A string or a _Data_ object for the message body.
   * @param tail An object containing protocol-dependent message footer.
   * @returns A _Message_ object containing the specified header, footer and body.
   */
  new(head: object, body?: string | Data, tail?: object);
}

declare var Message: MessageConstructor;
