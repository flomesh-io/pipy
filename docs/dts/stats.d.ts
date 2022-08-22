/**
 * Metric base interface.
 */
interface Metric {

  /**
   * Retrieves a sub-metric by labels.
   */
  withLabels(...labels: string[]): Metric;
}

/**
 * Counter metric.
 */
interface Counter extends Metric {

  /**
   * Sets the current value to zero.
   */
  zero(): void;

  /**
   * Increases the current value by a number.
   */
  increase(n?: number): void;

  /**
   * Decreases the current value by a number.
   */
  decrease(n?: number): void;
}

interface CounterConstructor {

  /**
   * Creates an instance of _Counter_.
   */
  new(name: string, labelNames?: string[]): Counter;
}

/**
 * Gauge metric.
 */
interface Gauge extends Metric {

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
  increase(n?: number): void;

  /**
   * Decreases the current value by a number.
   */
  decrease(n?: number): void;
}

interface GaugeConstructor {

  /**
   * Creates an instance of _Gauge_.
   */
  new(name: string, labelNames?: string[]): Gauge;
}

/**
 * Histogram metric.
 */
interface Histogram extends Metric {

  /**
   * Clears all buckets.
   */
  zero(): void;

  /**
   * Increases the bucket where a sample falls in.
   */
  observe(n: number): void;
}

interface HistogramConstructor {

  /**
   * Creates an instance of _Histogram_.
   */
  new(name: string, buckets: number[], labelNames?: string[]): Histogram;
}

interface Stats {
  Counter: CounterConstructor,
  Gauge: GaugeConstructor,
  Histogram: HistogramConstructor,
}

declare var stats: Stats;
