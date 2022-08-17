declare namespace Hessian {

  /**
   * Deserializes values in Hessian format.
   */
  declare function decode(data: Data): any;

  /**
   * Serializes values in Hessian format.
   */
  declare function encode(value: any): Data;

}