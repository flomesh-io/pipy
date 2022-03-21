/**
 * Represents a node in an XML document.
 *
 * @memberof XML
 */
class Node {

  /**
   * Creates an instance of Node.
   *
   * @param {string} name Tag name of the XML node.
   * @param {Object.<string, string>} [attributes] Attributes of the XML node as key-value pairs.
   * @param {Array.<Node|string>} [children] Array of child nodes.
   */
  constructor(name, attributes, children) {}

  /**
   * Tag name of the XML node.
   *
   * @type {string}
   * @readonly
   */
  name = '';

  /**
   * Attributes of the XML node as key-value pairs.
   *
   * @type {Object.<string, string>}
   * @readonly
   */
  attributes = null;

  /**
   * Array of child nodes.
   *
   * @type {Array.<Node|string>}
   * @readonly
   */
  children = null;
}

/**
 * @namespace
 */
var XML = {

  Node,

  /**
   * Parses text in a string as XML document and returns its root node.
   *
   * @memberof XML
   * @param {string} text String containing the text to parse.
   * @returns {Node} Root node of the XML document.
   */
  parse: function(text) {},

  /**
   * Formats an XML document in text.
   *
   * @memberof XML
   * @param {Node} node Root node of the XML document.
   * @param {number} [space] Number of spaces for the indentation.
   * @returns {string} String containing the formatted text.
   */
  stringify: function(node, space) {},

  /**
   * Parses text in a Data as XML document and returns its root node.
   *
   * @memberof XML
   * @param {Data} data Data containing the text to parse.
   * @returns {Node} Root node of the XML document.
   */
  decode: function(data) {},

  /**
   * Formats an XML document in text.
   *
   * @memberof XML
   * @param {Node} node Root node of the XML document.
   * @param {number} [space] Number of spaces for the indentation.
   * @returns {Data} Data containing the formatted text.
   */
  encode: function(node, space) {},

}
