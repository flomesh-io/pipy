/**
 * Node in an XML document.
 */
interface XMLNode {

  /**
   * Tag name.
   */
  readonly name: string;

  /**
   * An object containing key-value pairs of the attributes.
   */
  readonly attributes: { [name: string]: string };

  /**
   * Array of child nodes.
   */
  readonly children: (XMLNode | string)[];
}

interface XMLNodeConstructor {

  /**
   * Creates an instance of _Node_.
   *
   * @param name A tag name.
   * @param attributes An object containing key-value pairs of the attributes.
   * @param children An array of child _XML.Node_ objects.
   * @returns A _XML.Node_ object with the specified tag name, attributes and child nodes.
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
   *
   * @param text A string containing the text of an XML document.
   * @returns A _XML.Node_ object representing the root document node after parsing.
   */
  parse(text: string): XMLNode;

  /**
   * Writes document into text.
   *
   * @param rootNode A _XML.Node_ object representing the root document node.
   * @param space Number of spaces for indentation while formatting.
   * @returns A string containing the text of the XML document after formatting.
   */
  stringify(rootNode: XMLNode, space?: number): string;

  /**
   * Reads XML document from text.
   *
   * @param data A _Data_ object containing the text of an XML document.
   * @returns A _XML.Node_ object representing the root document node after parsing.
   */
  decode(data: Data): XMLNode;

  /**
   * Writes document into text.
   *
   * @param rootNode A _XML.Node_ object representing the root document node.
   * @param space Number of spaces for indentation while formatting.
   * @returns A string containing the text of the XML document after formatting.
   */
  encode(rootNode: XMLNode, space?: number): Data;
}

declare var XML: XML;
