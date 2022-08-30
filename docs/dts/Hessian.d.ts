declare interface Hessian {

  /**
   * Deserializes values in Hessian format.
   *
   * @param data The _Data_ to deserialize as Hessian format.
   * @returns A value of any type after deserialization.
   */
  decode(data: Data): any;

  /**
   * Serializes values in Hessian format.
   *
   * @param value A value of any type to serialize as Hessian format.
   * @returns A _Data_ object after serialization.
   */
  encode(value: any): Data;
}

declare var Hessian: Hessian;
