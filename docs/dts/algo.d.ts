/**
 * An ephemeral key-value storage that automatically cleans up old entries.
 */
interface Cache {

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

interface CacheConstructor {

  /**
   * Creates an instance of _Cache_.
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
   */
  produce(value: number): void;

  /**
   * Decreases the quota.
   */
  consume(value: number): number;
}

interface QuotaConstructor {

  /**
   * Creates an instance of _Quota_.
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
   */
  add(path: string, value: any): void;

  /**
   * Finds a route.
   */
  find(...pathSegments: string[]): any;
}

interface URLRouterConstructor {

  /**
   * Creates an instance of _URLRouter_.
   */
  new(routes: { [path: string]: any }): URLRouter;
}

/**
 * Load-balancer base class.
 */
interface LoadBalancer {

  /**
   * Allocates an resource item for the next selected target.
   */
  next(borrower?: any, key?: any): { id: string } | undefined;
}

/**
 * Load-balancer using consistent hashing.
 */
interface HashingLoadBalancer extends LoadBalancer {

  /**
   * Adds a target.
   */
  add(target: string): void;
}

interface HashingLoadBalancerConstructor {

  /**
   * Creates an instance of _HashingLoadBalancer_.
   */
  new(targets: string[], unhealthy?: Cache): HashingLoadBalancer;
}

/**
 * Load-balancer that rotates targets by round-robin algorithm.
 */
interface RoundRobinLoadBalancer extends LoadBalancer {

  /**
   * Sets weight of a target.
   */
  set(target: string, weight: number): void;
}

interface RoundRobinLoadBalancerConstructor {

  /**
   * Creates an instance of _RoundRobinLoadBalancer_.
   */
  new(targets: string[] | { [id: string]: number }, unhealthy?: Cache): RoundRobinLoadBalancer;
}

/**
 * Load-balancer that evens out workload among targets.
 */
interface LeastWorkLoadBalancer extends LoadBalancer {

  /**
   * Sets weight of a target.
   */
  set(target: string, weight: number): void;
}

interface LeastWorkLoadBalancerConstructor {

  /**
   * Creates an instance of _RoundRobinLoadBalancer_.
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
   */
  hash(value: any): number;

  /**
   * Generates a UUID.
   */
  uuid(): string;
}

declare var algo: Algo;
