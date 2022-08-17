declare namespace algo {

  /**
   * An ephemeral key-value storage that automatically cleans up old entries.
   */
  declare class Cache {

    /**
     * Creates an instance of _Cache_.
     */
    constructor(
      onAllocate?: (key: any) => any,
      onFree?: (key: any, value: any) => void,
      { size = 0, ttl = 0 }?: {
        size?: number,
        ttl?: number | string,
      }
    );

    /**
     * Looks up an entry.
     */
    get(key: any): any;

    /**
     * Creates or changes an entry.
     */
    set(key: any, value: any): void;

    /**
     * Deletes an entry.
     */
    remove(key: any): boolean;

    /**
     * Deletes all entries.
     */
    clear(): void;
  }

  /**
   * Keeps track of quota.
   */
  declare class Quota {

    /**
     * Creates an instance of _Quota_.
     */
    constructor(
      initialValue: number,
      { produce = 0, per = 0 }?: {
        produce?: number,
        per?: number | string,
      }
    );

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
     */
    produce(value: number): void;

    /**
     * Decreases the quota.
     */
    consume(value: number): number;
  }

  /**
   * Path-based routing algorithm.
   */
  declare class URLRouter {

    /**
     * Creates an instance of _URLRouter_.
     */
    constructor(routes: { [path: string]: any });

    /**
     * Appends a route.
     */
    add(path: string, value: any): void;

    /**
     * Finds a route.
     */
    find(...pathSegments: string[]): any;
  }

  /**
   * Load-balancer base class.
   */
  declare interface LoadBalancer {

    /**
     * Allocates an resource item for the next selected target.
     */
    next(borrower?: any, key?: any): { id: string } | undefined;
  }

  /**
   * Load-balancer using consistent hashing.
   */
  declare class HashingLoadBalancer extends LoadBalancer {

    /**
     * Creates an instance of _HashingLoadBalancer_.
     */
    constructor(targets: string[], unhealthy?: Cache);

    /**
     * Adds a target.
     */
    add(target: string): void;
  }

  /**
   * Load-balancer that rotates targets by round-robin algorithm.
   */
  declare class RoundRobinLoadBalancer extends LoadBalancer {

    /**
     * Creates an instance of _RoundRobinLoadBalancer_.
     */
    constructor(targets: string[] | { [id: string]: number }, unhealthy?: Cache);

    /**
     * Sets weight of a target.
     */
    set(target: string, weight: number): void;
  }

  /**
   * Load-balancer that evens out workload among targets.
   */
   declare class LeastWorkLoadBalancer extends LoadBalancer {

    /**
     * Creates an instance of _RoundRobinLoadBalancer_.
     */
    constructor(targets: string[] | { [id: string]: number }, unhealthy?: Cache);

    /**
     * Sets weight of a target.
     */
    set(target: string, weight: number): void;
  }

  /**
   * Gets the hash of a value of any type.
   */
  function hash(value: any): number;

  /**
   * Generates a UUID.
   */
  function uuid(): string;
}
