/**
 *
 */
class Data {

  /**
   * @param {string|Data} [data]
   * @param {string} [encoding]
   */
  constructor(data, encoding) {}

  /**
   * @type {number}
   * @readonly
   */
  size = 0;

  /**
   * @param {string|Data} data
   * @returns {Data}
   */
  push(data) {}

  /**
   * @param {number} size 
   * @returns {Data}
   */
  shift(size) {}

  /**
   * @param {string} [encoding]
   * @returns {string}
   */
  toString(encoding) {}
}
