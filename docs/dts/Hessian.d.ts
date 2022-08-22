declare interface Hessian {

  /**
   * Deserializes values in Hessian format.
   */
  decode(data: Data): any;

  /**
   * Serializes values in Hessian format.
   */
  encode(value: any): Data;
}

declare var Hessian: Hessian;
