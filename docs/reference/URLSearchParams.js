/**
 *
 */
class URLSearchParams {
  /**
   * @param {string|Object} search 
   */
  constructor(search) {}

  /**
   * @param {string} name 
   * @returns {string[]}
   */
  getAll(name) {}

  /**
   * @param {string} name
   * @returns {string}
   */
  get(name) {}

  /**
   * @param {string} name 
   * @param {*} value 
   */
  set(name, value) {}

  /**
   * @returns {string}
   */
  toString() {}

  /**
   * @returns {Object.<string, string|string[]>}
   */
  toObject() {}
}
