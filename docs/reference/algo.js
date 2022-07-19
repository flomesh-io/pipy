/**
 * An ephemeral key-value storage that automatically cleans up old entries.
 *
 * @memberof algo
 */
class Cache {

  /**
   * @callback CacheAddCB
   * @param {*} key
   * @return {*}
   *
   * @callback CacheRemoveCB
   * @param {*} key
   * @param {*} value
   */

  /**
   * Creates an instance of Cache.
   *
   * @param {CacheAddCB} [add] Callback when a new entry needs to be added.
   * @param {CacheRemoveCB} [remove] Callback when an entry is about to be erased.
   * @param {Object} [options] Options including size and ttl.
   */
  constructor(add, remove, options) {}

  /**
   * Looks up in Cache for the entry with the given key.
   *
   * @param {*} key Key to look up.
   * @returns {*} Value corresponding to the key.
   */
  get(key) {}

  /**
   * Creates or updates an entry in Cache.
   *
   * @param {*} key Key of the entry.
   * @param {*} value Value of the entry.
   */
  set(key, value) {}

  /**
   * Deletes an entry in Cache.
   *
   * @param {*} key Key of the entry to delete.
   */
  remove(key) {}

  /**
   * Deletes all entries in Cache.
   */
  clear() {}
}

/**
 * @memberof algo
 */
class Quota {

  /**
   * @param {number} initialValue
   * @param {Object} [options]
   * @param {number|string} [options.per]
   */
  constructor(initialValue, options) {}

  /**
   * @type {number}
   * @readonly
   */
  initial = 0;

  /**
   * @type {number}
   * @readonly
   */
  current = 0;

  reset() {}

  /**
   * @param {number} value
   */
  produce(value) {}

  /**
   * @param {number} value
   * @returns {number}
   */
  consume(value) {}
}

/**
 * Path-based routing algorithm using a search tree.
 *
 * @memberof algo
 */
class URLRouter {

  /**
   * @param {Object} routes Path-to-value mapping table.
   */
  constructor(routes) {}

  /**
   * Adds a path-to-value mapping.
   *
   * @param {string} url Path to map.
   * @param {*} value Value mapped to the path.
   */
  add(url, value) {}

  /**
   * Find the value that is mapped to the given path.
   *
   * @param {string} path Parts of the path to search for.
   * @returns {*} The value mapped to the path if found.
   */
  find(path) {}
}

/**
 * @memberof algo
 */
class HashingLoadBalancer {

  /**
   * @param {string[]} targets 
   */
  constructor(targets) {}

  /**
   * @param {string} target
   */
  add(target) {}

  /**
   * @returns {string}
   */
  select() {}

  /**
   * @param {string} target 
   */
  deselect(target) {}

  /**
   * @returns {Object}
   */
  next() {}
}

/**
 * @memberof algo
 */
class RoundRobinLoadBalancer {

  /**
   * @param {string[]|Object.<string, number>} targets 
   */
  constructor(targets) {}

  /**
   * @param {string} target
   * @param {number} weight
   */
  set(target, weight) {}

  /**
   * @returns {string}
   */
  select() {}

  /**
   * @param {string} target 
   */
  deselect(target) {}

  /**
   * @returns {Object}
   */
  next() {}
}

/**
 * @memberof algo
 */
class LeastWorkLoadBalancer {

  /**
   * @param {string[]|Object.<string, number>} targets 
   */
  constructor(targets) {}

  /**
   * @param {string} target
   * @param {number} weight
   */
  set(target, weight) {}

  /**
   * @returns {string}
   */
  select() {}

  /**
   * @param {string} target 
   */
  deselect(target) {}

  /**
   * @returns {Object}
   */
  next() {}
}

/**
 * @memberof algo
 */
class ResourcePool {

  /**
   * @param {(tag) => value} allocator
   */
  constructor(allocator) {}

  /**
   * @param {*} [tag]
   * @returns {*}
   */
  allocate(tag) {}

  /**
   * @param {*} resource
   */
  free(resource) {}
}

/**
 * @memberof algo
 */
class Percentile {

  /**
   * @param {number[]} buckets 
   */
  constructor(buckets) {}

  /**
   *
   */
  reset() {}

  /**
   * @param {number} sample 
   */
  observe(sample) {}

  /**
   * @param {number} percentage
   * @returns {number}
   */
  calculate(percentage) {}
}

/**
 * @namespace
 */
var algo = {
  Cache,
  Quota,
  URLRouter,
  HashingLoadBalancer,
  RoundRobinLoadBalancer,
  LeastWorkLoadBalancer,
  ResourcePool,
  Percentile,
}
