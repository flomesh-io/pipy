/**
 * @memberof XML
 */
class Node {

  /**
   * @param {string} name
   * @param {Object.<string, string>} [attributes]
   * @param {Array.<Node|string>} [children]
   */
  constructor(name, attributes, children) {}

  /**
   * @type {string}
   * @readonly
   */
  name = '';

  /**
   * @type {Object.<string, string>}
   * @readonly
   */
  attributes = null;

  /**
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
   * @param {string} text
   * @returns {Node}
   */
  parse: function(text) {},

  /**
   * @param {Node} node
   * @param {number} [space]
   * @returns {string}
   */
  stringify: function(node, space) {},

  /**
   * @param {Data} data
   * @returns {Node}
   */
  decode: function(data) {},

  /**
   * @param {Node} node
   * @param {number} [space]
   * @returns {Data}
   */
  encode: function(node, space) {},

}
