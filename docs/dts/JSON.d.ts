declare interface JSON {

  /**
   * Deserializes values in JSON format.
   */
  static parse(text: string): any;

  /**
   * Serializes values in JSON format.
   */
  static stringify(
    value: any,
    replacer?: (key: string, value: any) => any,
    space?: number
  ): any;

  /**
   * Deserializes values in JSON format.
   */
  static decode(data: Data): any;

  /**
   * Serializes values in JSON format.
   */
  static encode(
    value: any,
    replacer?: (key: string, value: any) => any,
    space?: number
  ): any;

}
