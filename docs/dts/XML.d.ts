declare namespace XML {

  /**
   * Node in an XML document.
   */
  class Node {

    /**
     * Creates an instance of _Node_.
     */
    constructor(
      name: string,
      attributes?: { [name: string]: string },
      children?: (Node | string)[]
    );

    /**
     * Tag name.
     */
    readonly name: string;

    /**
     * Attributes.
     */
    readonly attributes: { [name: string]: string };

    /**
     * Child nodes.
     */
    readonly children: (Node | string)[];

  }

  /**
   * Reads XML document from text.
   */
  function parse(text: string): Node;

  /**
   * Writes document into text.
   */
  function stringify(rootNode: Node, space?: number): string;

  /**
   * Reads XML document from text.
   */
  function decode(data: Data): Node;

  /**
   * Writes document into text.
   */
  function encode(rootNode: Node, space?: number): Data;

}
