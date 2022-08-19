/// <reference no-default-lib="true"/>

interface JSON {

  /**
   * Deserializes values in JSON format.
   */
  parse(text: string): any;

  /**
   * Serializes values in JSON format.
   */
  stringify(
    value: any,
    replacer?: (key: string, value: any) => any,
    space?: number
  ): any;

  /**
   * Deserializes values in JSON format.
   */
  decode(data: Data): any;

  /**
   * Serializes values in JSON format.
   */
  encode(
    value: any,
    replacer?: (key: string, value: any) => any,
    space?: number
  ): any;
}

declare var JSON: JSON;
