declare namespace JSON {

  /**
   * Deserializes values in JSON format.
   */
  declare function parse(text: string): any;

  /**
   * Serializes values in JSON format.
   */
  declare function stringify(
    value: any,
    replacer?: (key: string, value: any) => any,
    space?: number
  ): any;

  /**
   * Deserializes values in JSON format.
   */
  declare function decode(data: Data): any;

  /**
   * Serializes values in JSON format.
   */
  declare function encode(
    value: any,
    replacer?: (key: string, value: any) => any,
    space?: number
  ): any;

}
