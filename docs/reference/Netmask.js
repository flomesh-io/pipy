/**
 *
 */
class Netmask {

  /**
   * @param {string} cidr 
   */
  constructor(cidr) {}

  /**
   * @type {string}
   * @readonly
   */
  base = '';

  /**
   * @type {string}
   * @readonly
   */
  mask = '';

  /**
   * @type {number}
   * @readonly
   */
  bitmask = 0;

  /**
   * @type {string}
   * @readonly
  */
  hostmask = '';

  /**
   * @type {string}
   * @readonly
   */
  broadcast = '';

  /**
   * @type {number}
   * @readonly
   */
  size = 0;

  /**
   * @type {string}
   * @readonly
   */
  first = '';

  /**
   * @type {string}
   * @readonly
   */
  last = '';

  /**
   * @param {string} ip
   * @returns {boolean}
   */
  contains(ip) {}

  /**
   * @returns {string}
   */
  next() {}
}
