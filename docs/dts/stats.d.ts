declare namespace stats {

  /**
   * Metric base interface.
   */
  declare interface Metric {

    /**
     * Retrieves a sub-metric by labels.
     */
    withLabels(...labels: string[]): Metric;
  }

  /**
   * Counter metric.
   */
  declare class Counter implements Metric {

    /**
     * Creates an instance of _Counter_.
     */
    constructor(name: string, labelNames?: string[]);

    /**
     * Sets the current value to zero.
     */
    zero(): void;
 
    /**
     * Increases the current value by a number.
     */
    increase(n?: number = 1): void;
 
    /**
     * Decreases the current value by a number.
     */
    decrease(n?: number = 1): void;
  }

  /**
   * Gauge metric.
   */
  declare class Gauge implements Metric {

    /**
     * Creates an instance of _Metric_.
     */
    constructor(name: string, labelNames?: string[]);

    /**
     * Sets the current value to zero.
     */
    zero(): void;

    /**
     * Sets the current value.
     */
    set(n: number): void;

    /**
     * Increases the current value by a number.
     */
    increase(n?: number = 1): void;

    /**
     * Decreases the current value by a number.
     */
    decrease(n?: number = 1): void;
  }

  /**
   * Histogram metric.
   */
  declare class Histogram implements Metric {

    /**
     * Creates an instance of _Histogram_.
     */
    constructor(name: string, buckets: number[], labelNames?: string[]);

    /**
     * Clears all buckets.
     */
    zero(): void;

    /**
     * Increases the bucket where a sample falls in.
     */
    observe(n: number): void;
  }

}
