interface JSON {

  /**
   * Deserializes a value from JSON format.
   *
   * @param text A string to deserialize as JSON format.
   * @param reviver A callback function that receives a deserialized value via its
   *   three parameters (_key_, _value_ and _container_) and returns a replacement value
   *   as result.
   *   
   * @returns A value of any type after deserialization.
   */
  parse(
    text: string,
    reviver?: (key: string, value: any) => any
  ): any;

  /**
   * Serializes a value in JSON format.
   *
   * @param value A value of any type to serialize in JSON format.
   * @param replacer A callback function that receives a value being serializing via its
   *   three parameters (_key_, _value_ and _container_) and returns a replacement value
   *   to be serialized instead.
   * @param space Number of spaces for the indentation, or zero for no indentation. Default is `0`.
   * @returns A string after serialization.
   */
  stringify(
    value: any,
    replacer?: (key: string, value: any) => any,
    space?: number
  ): any;

  /**
   * Deserializes a value from JSON format.
   *
   * @param data A _Data_ object to deserialize as JSON format.
   * @param reviver A callback function that receives a deserialized value via its
   *   three parameters (_key_, _value_ and _container_) and returns a replacement value
   *   as result.
   * @returns A value of any type after deserialization.
   */
  decode(
    data: Data,
    reviver?: (key: string, value: any, container: Object) => any
  ): any;

  /**
   * Serializes a values in JSON format.
   * @param value A value of any type to serialize in JSON format.
   * @param replacer A callback function that receives a value being serializing via its
   *   three parameters (_key_, _value_ and _container_) and returns a replacement value
   *   to be serialized instead.
   * @param space Number of spaces for the indentation, or zero for no indentation. Default is `0`.
   * @returns A _Data_ object after serialization.
   */
  encode(
    value: any,
    replacer?: (key: string, value: any, container: Object) => any,
    space?: number
  ): Data;
}

declare var JSON: JSON;
