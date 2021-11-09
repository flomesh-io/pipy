/**
 * @namespace
 */
var JSON = {

  /**
   * @param {string} text
   * @returns {*}
   */
  parse: function(text) {},

  /**
   * @param {*} value
   * @param {(key, value) => *} [replacer]
   * @param {number} [space]
   * @returns {string}
   */
  stringify: function(value, replacer, space) {},

  /**
   * @param {Data} data
   * @returns {*}
   */
  decode: function(data) {},

  /**
   * @param {*} value
   * @param {(key, value) => *} [replacer]
   * @param {number} [space]
   * @returns {Data}
   */
  encode: function(value, replacer, space) {},
}
