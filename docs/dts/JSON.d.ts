interface JSON {

  /**
   * Deserializes a value from JSON format.
   *
   * @param text A string to deserialize as JSON format.
   * @returns A value of any type after deserialization.
   */
  parse(text: string): any;

  /**
   * Serializes a value in JSON format.
   *
   * @param value A value of any type to serialize in JSON format.
   * @param replacer A callback function that receives _key_ and _value_ pairs from
   *   the objects being serialized and returns a replacement value to be serialized instead.
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
   * @returns A value of any type after deserialization.
   */
  decode(data: Data): any;

  /**
   * Serializes a values in JSON format.
   * @param value A value of any type to serialize in JSON format.
   * @param replacer A callback function that receives _key_ and _value_ pairs from
   *   the objects being serialized and returns a replacement value to be serialized instead.
   * @param space Number of spaces for the indentation, or zero for no indentation. Default is `0`.
   * @returns A _Data_ object after serialization.
   */
  encode(
    value: any,
    replacer?: (key: string, value: any) => any,
    space?: number
  ): Data;
}

declare var JSON: JSON;
