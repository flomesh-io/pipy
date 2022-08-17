declare interface Hessian {

  /**
   * Deserializes values in Hessian format.
   */
  static decode(data: Data): any;

  /**
   * Serializes values in Hessian format.
   */
  static encode(value: any): Data;

}
