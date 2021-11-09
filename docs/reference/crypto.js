/**
 * @memberof crypto
 */
class PublicKey {

  /**
   * @param {string|Data} content
   * @param {Object} [options]
   * @param {string} [options.aliasType]
   */
  constructor(content, options) {}
}

/**
 * @memberof crypto
 */
class PrivateKey {

  /**
   * @param {string|Data} content
   * @param {Object} [options]
   * @param {string} [options.aliasType]
   */
  constructor(content, options) {}
}


/**
 * @memberof crypto
 */
class Certificate {

  /**
   * @param {string|Data} content 
   */
  constructor(content) {}
}

/**
 * @memberof crypto
 */
class CertificateChain {

  /**
   * @param {string|Data} content 
   */
  constructor(content) {}
}

/**
 * @memberof crypto
 */
class Cipher {

  /**
   * @param {string} algorithm
   * @param {Object} [options]
   * @param {string|Data} [options.key]
   * @param {string|Data} [options.iv]
   */
  constructor(algorithm, options) {}

  /**
   * @param {string|Data} data
   * @returns {Data}
   */
  update(data) {}

  /**
   * @returns {Data}
   */
  final() {}
}

/**
 * @memberof crypto
 */
class Decipher {

  /**
   * @param {string} algorithm 
   * @param {Object} options 
   * @param {string|Data} [options.key]
   * @param {string|Data} [options.iv]
   */
  constructor(algorithm, options) {}

  /**
   * @param {string|Data} data
   * @returns {Data}
   */
  update(data) {}

  /**
   * @return {Data}
   */
  final() {}
}

/**
 * @memberof crypto
 */
class Hash {

  /**
   * @param {string} algorithm 
   */
  constructor(algorithm) {}

  /**
   * @param {string|Data} data
   * @param {string} [encoding]
   */
  update(data, encoding) {}

  /**
   * @param {string} [encoding]
   * @returns {string|Data}
   */
  digest(encoding) {}
}

/**
 * @memberof crypto
 */
class Hmac {

  /**
   * @param {string} algorithm 
   * @param {string|Data} key 
   */
  constructor(algorithm, key) {}

  /**
   * @param {string|Data} data
   * @param {string} [encoding]
   */
  update(data, encoding) {}

  /**
   * @param {string} [encoding]
   * @returns {string|Data}
   */
  digest(encoding) {}
}

/**
 * @memberof crypto
 */
class Sign {

  /**
   * @param {string} algorithm
   */
  constructor(algorithm) {}

  /**
   * @param {string|Data} data
   * @param {string} [encoding] 
   */
  update(data, encoding) {}

  /**
   * @param {PrivateKey} key
   * @param {encoding} [encoding]
   * @param {Object} [options]
   * @param {Data} [options.id]
   */
  sign(key, encoding, options) {}
}

/**
 * @memberof crypto
 */
class Verify {

  /**
   * @param {string} algorithm
   */
  constructor(algorithm) {}

  /**
   * @param {string|Data} data
   * @param {string} [encoding] 
   */
  update(data, encoding) {}

  /**
   * @param {PublicKey} key
   * @param {string|Data} signature
   * @param {encoding} [encoding]
   * @param {Object} [options]
   * @param {Data} [options.id]
   * @returns {boolean}
   */
  verify(key, signature, encoding, options) {}
}

/**
 * @memberof crypto
 */
class JWK {

  /**
   * @param {Object} json
   */
  constructor(json) {}

  /**
   * @type {boolean}
   * @readonly
   */
  isValid = false;
}

/**
 * @memberof crypto
 */
class JWT {

  /**
   * @param {string} token 
   */
  constructor(token) {}

  /**
   * @type {boolean}
   * @readonly
   */
  isValid = false;

  /**
   * @type {Object}
   * @readonly
   */
  header = null;

  /**
   * @type {Object}
   * @readonly
   */
  payload = null;

  /**
   * 
   * @param {string|Data|JWK|PublicKey} key
   * @returns {boolean}
   */
  verify(key) {}
}

/**
 * @namespace
 */
var crypto = {
  PublicKey,
  PrivateKey,
  Certificate,
  CertificateChain,
  Cipher,
  Decipher,
  Hash,
  Hmac,
  Sign,
  Verify,
  JWT,
  JWK,
}
