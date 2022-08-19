/// <reference no-default-lib="true"/>
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
   * Message body or payload.
   */
  body?: Data;
}

interface MessageConstructor {

  /**
   * Creates an instance of _Message_.
   */
  new(body?: string | Data): Message;

  /**
   * Creates an instance of _Message_.
   */
  new(head: object, body?: string | Data, tail?: object);
}

declare var Message: MessageConstructor;
