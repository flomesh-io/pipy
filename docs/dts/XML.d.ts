/// <reference no-default-lib="true"/>

/**
 * Node in an XML document.
 */
interface XMLNode {

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
  readonly children: (XMLNode | string)[];
}

interface XMLNodeConstructor {

  /**
   * Creates an instance of _Node_.
   */
  new(
    name: string,
    attributes?: { [name: string]: string },
    children?: (XMLNode | string)[]
  ): XMLNode;
}

interface XML {

  /**
   * A node in an XML document.
   */
  Node: XMLNodeConstructor;

  /**
   * Reads XML document from text.
   */
  parse(text: string): XMLNode;

  /**
   * Writes document into text.
   */
  stringify(rootNode: XMLNode, space?: number): string;

  /**
   * Reads XML document from text.
   */
  decode(data: Data): XMLNode;

  /**
   * Writes document into text.
   */
  encode(rootNode: XMLNode, space?: number): Data;
}

declare var XML: XML;
