/**
 * @memberof os
 */
class Stat {

  /**
   * @type {number}
   * @readonly
   */
  dev = 0;


  /**
   * @type {number}
   * @readonly
   */
  ino = 0;

  /**
   * @type {number}
   * @readonly
   */
  mode = 0;

  /**
   * @type {number}
   * @readonly
   */
  nlink = 0;

  /**
   * @type {number}
   * @readonly
   */
  uid = 0;

  /**
   * @type {number}
   * @readonly
   */
  gid = 0;

  /**
   * @type {number}
   * @readonly
   */
  rdev = 0;

  /**
   * @type {number}
   * @readonly
   */
  size = 0;

  /**
   * @type {number}
   * @readonly
   */
  blksize = 0;

  /**
   * @type {number}
   * @readonly
   */
  blocks = 0;

  /**
   * @type {number}
   * @readonly
   */
  atime = 0;

  /**
   * @type {number}
   * @readonly
   */
  mtime = 0;

  /**
   * @type {number}
   * @readonly
   */
  ctime = 0;

  /**
   * @returns {boolean}
   */
  isFile() {}

  /**
   * @returns {boolean}
   */
  isDirectory() {}

  /**
   * @returns {boolean}
   */
  isCharacterDevice() {}

  /**
   * @returns {boolean}
   */
  isBlockDevice() {}

  /**
   * @returns {boolean}
   */
  isFIFO() {}

  /**
   * @returns {boolean}
   */
  isSymbolicLink() {}

  /**
   * @returns {boolean}
   */
  isSocket() {}
}

/**
 * @namespace
 */
var os = {

  Stat,

  /**
   * @kind member
   * @type {Object.<string, string>}
   * @readonly
   */
  env: {},

  /**
   * @param {string} filename
   * @returns {Stat}
   */
  stat: function(filename) {},

  /**
   * @param {string} filename
   * @returns {Data}
   */
  readFile: function(filename) {},

  /**
   * @param {string} filename 
   * @param {string|Data} content 
   */
  writeFile: function(filename, content) {},
}
