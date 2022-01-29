/**
 * @memberof stats
 */
class Counter {

  /**
   * Create a new metric of counter type.
   *
   * @param {string} name Name of the metric.
   * @param {string[]} [labelNames] Label names for sub-metrics.
   */
  constructor(name, labelNames) {}

  /**
   * Retrieves a sub-metric by labels.
   *
   * @param  {...any} labels Labels by which the sub-metric is looked up.
   * @returns {Counter} The sub-metric with the given labels.
   */
  withLabels(...labels) {}

  /**
   * Set the counter to zero.
   */
  zero() {}

  /**
   * Increase the counter by a number.
   *
   * @param {number} [n = 1] Number to add to the counter.
   */
  increase(n) {}

  /**
   * Decrease the counter by a number.
   *
   * @param {number} [n = 1] Number to subtract from the counter.
   */
  decrease(n) {}
}

/**
 * @memberof stats
 */
class Gauge {

  /**
   * Create a new metric of gauge type.
   *
   * @param {string} name Name of the metric.
   * @param {string[]} [labelNames] Label names for sub-metrics.
   */
  constructor(name, labelNames) {}

  /**
   * Retrieves a sub-metric by labels.
   *
   * @param  {...any} labels Labels by which the sub-metric is looked up.
   * @returns {Gauge} The sub-metric with the given labels.
   */
  withLabels(...labels) {}

  /**
   * Set the gauge to zero.
   */
  zero() {}

  /**
   * Set the gauge to an arbitrary number.
   *
   * @param {*} n New value to change to.
   */
  set(n) {}

  /**
   * Increase the gauge by a number.
   *
   * @param {number} [n = 1] Number to add to the gauge.
   */
  increase(n) {}

  /**
   * Decrease the gauge by a number.
   *
   * @param {number} [n = 1] Number to subtract from the gauge.
   */
  decrease(n) {}
}

/**
 * @memberof stats
 */
class Histogram {

  /**
   * Create a new metric of histogram type.
   *
   * @param {string} name Name of the metric.
   * @param {number[]} [buckets] Upper inclusive bounds of the buckets.
   * @param {string[]} [labelNames] Label names for sub-metrics.
   */
  constructor(name, buckets, labelNames) {}

  /**
   * Retrieves a sub-metric by labels.
   *
   * @param  {...any} labels Labels by which the sub-metric is looked up.
   * @returns {Histogram} The sub-metric with the given labels.
   */
  withLabels(...labels) {}

  /**
   * Set all buckets to zero.
   */
  zero() {}

  /**
   * Increase the bucket that an observation falls into.
   *
   * @param {*} n Value of the observation.
   */
  observe(n) {}
}

/**
 * @namespace
 */
var stats = {
  Counter,
  Gauge,
  Histogram,
}
