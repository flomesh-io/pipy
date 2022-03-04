/**
 * Data is an array of read-only octets.
 * It can be read from network as an input event or created by script and sent out over to network.
 */
class Data {

  /**
   * Creates an instance of Data.
   *
   * @param {string|Data} [data] Data content.
   * @param {string} [encoding] Encoding to use if content is a string.
   */
  constructor(data, encoding) {}

  /**
   * Number of bytes.
   *
   * @type {number}
   * @readonly
   */
  size = 0;

  /**
   * Appends data to the end.
   *
   * @param {string|Data} data Data to append to the end.
   * @returns {Data} The same Data object.
   */
  push(data) {}

  /**
   * Removes data from the beginning.
   *
   * @param {number} size Number of bytes to remove from the beginning.
   * @returns {Data} Removed data.
   */
  shift(size) {}

  /**
   * Convert the data to a string.
   *
   * @param {string} [encoding] Encoding to use in conversion.
   * @returns {string} String after conversion.
   */
  toString(encoding) {}
}

/**
 * Creates an instance of Data from a string.
 *
 * @param {string} text Text to encode or decode.
 * @param {string} [encoding] Encoding to use.
 * @returns {Data} Data object created.
 * @memberof Data
 */
Data.from = function(text, encoding) {}
