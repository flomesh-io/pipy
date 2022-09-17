/**
 * An ephemeral key-value storage that automatically cleans up old entries.
 */
interface Cache {

  /**
   * Looks up an entry.
   *
   * @param key The key of the entry to look up. Can be of any type.
   * @returns The value of the entry.
   */
  get(key: any): any;

  /**
   * Creates or changes an entry.
   *
   * @param key The key of the entry to create or change. Can be of any type.
   * @param value The value of the entry.
   */
  set(key: any, value: any): void;

  /**
   * Deletes an entry.
   *
   * @param key The key of the entry to delete.
   * @returns A boolean value indicating whether the entry being deleted was found.
   */
  remove(key: any): boolean;

  /**
   * Deletes all entries.
   */
  clear(): void;
}

interface CacheConstructor {

  /**
   * Creates an instance of _Cache_.
   *
   * @param onAllocate A function to be called when a queried entry does not exist.
   *   It receives the key of the entry and is supposed to return the value of that entry.
   * @param onFree A function to be called when an entry is deleted.
   *   It receives 2 parameters: the key and the value of the entry being deleted.
   * @param options Options including:
   *   - _size_ - Maximum number of entries allowed in the cache.
   *   - _ttl_ - Time-to-live for the entries in the cache.
   *       Can be a number in seconds or a string with one of the time unit suffixes such as `'s'`, `'m'` and `'h'`.
   * @returns An empty _Cache_ object.
   */
  new(
    onAllocate?: (key: any) => any,
    onFree?: (key: any, value: any) => void,
    options?: {
      size?: number,
      ttl?: number | string,
    }
  ): Cache;
}

/**
 * Keeps track of quota.
 */
interface Quota {

  /**
   * Initial quota.
   */
  initial: number;

  /**
   * Current quota.
   */
  current: number;

  /**
   * Reset quota to the initial value.
   */
  reset(): void;

  /**
   * Increases the quota.
   *
   * @param value Number to increase the current quota by.
   */
  produce(value: number): void;

  /**
   * Decreases the quota.
   *
   * @param value Number to decrease the current quota by.
   * @returns The actual number by which the quota has decreased.
   */
  consume(value: number): number;
}

interface QuotaConstructor {

  /**
   * Creates an instance of _Quota_.
   *
   * @param initialValue Initial quota. Must be a number.
   * @param options Options including:
   *   - _produce_ - Number by which the quota increases each time it recovers.
   *   - _per_ - Time interval by which the quota recovers automatically.
   *       Can be a number in seconds or a string with one of the time unit suffixes such as `'s'`, `'m'` and `'h'`.
   * @returns A _Quota_ object with the specified initial quota.
   */
  new(
    initialValue: number,
    options?: {
      produce?: number,
      per?: number | string,
    }
  ): Quota;
}

/**
 * Path-based routing algorithm.
 */
interface URLRouter {

  /**
   * Appends a route.
   *
   * @param path A string containing a path.
   * @param value The value that the given _path_ is mapped to.
   */
  add(path: string, value: any): void;

  /**
   * Finds a route.
   *
   * @param pathSegments A series of strings that make up a path to look up.
   * @returns The value that the queried path maps to, or `undefined` if the path is not found.
   */
  find(...pathSegments: string[]): any;
}

interface URLRouterConstructor {

  /**
   * Creates an instance of _URLRouter_.
   *
   * @param routes An object of key-value pairs where keys are the paths and values are what the paths map to.
   * @returns A _URLRouter_ object with the provided initial mapping.
   */
  new(routes: { [path: string]: any }): URLRouter;
}

/**
 * Load-balancer base class.
 */
interface LoadBalancer {

  /**
   * Allocates an resource item for the next selected target.
   *
   * @param borrower A value of any type identifying the owner of the allocated resource.
   *   It defaults to the current value of `__inbound` if not present.
   * @param tag A value of any type as a tag given to the selected target so that the same target can be selected
   *   next time the same tag is requested.
   * @param unhealthy A _Cache_ object storing excluded targets that should not be picked.
   * @returns A resource object containing a field named `id` for the allocated target.
   */
  next(borrower?: any, tag?: any, unhealthy?: Cache): { id: string } | undefined;
}

/**
 * Load-balancer using consistent hashing.
 */
interface HashingLoadBalancer extends LoadBalancer {

  /**
   * Adds a target.
   *
   * @param target A string representing the target to add in the target list.
   */
  add(target: string): void;
}

interface HashingLoadBalancerConstructor {

  /**
   * Creates an instance of _HashingLoadBalancer_.
   *
   * @param targets An array of strings. Each string represents a target.
   * @param unhealthy A _Cache_ object storing _unhealthy_ targets.
   * @returns A _HashingLoadBalancer_ object with the specified targets.
   */
  new(targets: string[], unhealthy?: Cache): HashingLoadBalancer;
}

/**
 * Load-balancer that rotates targets by round-robin algorithm.
 */
interface RoundRobinLoadBalancer extends LoadBalancer {

  /**
   * Sets weight of a target.
   *
   * @param target A string representing the target to add or set weight for.
   * @param weight A number as the weight of the target.
   */
  set(target: string, weight: number): void;
}

interface RoundRobinLoadBalancerConstructor {

  /**
   * Creates an instance of _RoundRobinLoadBalancer_.
   *
   * @param targets An array of strings representing the targets, or an object of key-value pairs
   *   where keys are the targets and values are the weights.
   * @param unhealthy A _Cache_ object storing _unhealthy_ targets.
   * @returns A _RoundRobinLoadBalancer_ object with the specified targets.
   */
  new(targets: string[] | { [id: string]: number }, unhealthy?: Cache): RoundRobinLoadBalancer;
}

/**
 * Load-balancer that evens out workload among targets.
 */
interface LeastWorkLoadBalancer extends LoadBalancer {

  /**
   * Sets weight of a target.
   *
   * @param target A string representing the target to add or set weight for.
   * @param weight A number as the weight of the target.
   */
  set(target: string, weight: number): void;
}

interface LeastWorkLoadBalancerConstructor {

  /**
   * Creates an instance of _LeastWorkLoadBalancer_.
   *
   * @param targets An array of strings representing the targets, or an object of key-value pairs
   *   where keys are the targets and values are the weights.
   * @param unhealthy A _Cache_ object storing _unhealthy_ targets.
   * @returns A _LeastWorkLoadBalancer_ object with the specified targets.
   */
  new(targets: string[] | { [id: string]: number }, unhealthy?: Cache): LeastWorkLoadBalancer;
}

interface Algo {
  Cache: CacheConstructor;
  Quota: QuotaConstructor;
  URLRouter: URLRouterConstructor;
  HashingLoadBalancer: HashingLoadBalancerConstructor;
  RoundRobinLoadBalancer: RoundRobinLoadBalancerConstructor;
  LeastWorkLoadBalancer: LeastWorkLoadBalancerConstructor;

  /**
   * Gets the hash of a value of any type.
   *
   * @param value A value of any type.
   * @returns A hash number calculated from the given value.
   */
  hash(value: any): number;

  /**
   * Generates a UUID.
   *
   * @returns A string containing the generated UUID.
   */
  uuid(): string;
}

declare var algo: Algo;
