/**
 * IPv4 CIDR address block.
 */
interface Netmask {

  /**
   * The Internet Protocol version, 4 or 6.
   */
  readonly version: 4 | 6;

  /**
   * Subnet mask width.
   */
  readonly bitmask: number;

  /**
   * The IP address part of CIDR notation.
   */
  readonly ip: string;

  /**
   * Base address in dot-decimal notation.
   */
  readonly base: string;

  /**
   * Subnet mask in dot-decimal notation.
   */
  readonly mask: string;

  /**
   * Host mask in dot-decimal notation.
   */
  readonly hostmask: string;

  /**
   * Broadcast address in dot-decimal notation.
   */
  readonly broadcast: string;

  /**
   * Size of the address block.
   */
  readonly size: number;

  /**
   * First address in the address block.
   */
  readonly first: string;

  /**
   * Last address in the address block.
   */
  readonly last: string;

  /**
   * Extracts the number components of the IP address.
   *
   * @returns An array of numbers composing the IP address with length of 4 for IPv4 or 8 for IPv6.
   */
  decompose(): number[];

  /**
   * Checks if an address belongs to the block.
   *
   * @param ip A string containing an IP address in dot-decimal notation.
   * @returns A boolean indicating if the specified IP address belongs to the block.
   */
  contains(ip: string): boolean;

  /**
   * Allocates an address from the block.
   *
   * @returns A string containing the allocated IP address in dot-decimal notation.
   */
  next(): string;
}

interface NetmaskConstructor {

  /**
   * Creates an instance of _Netmask_.
   *
   * @param cidr A string containing an IPv4 address block in CIDR notation.
   * @returns A _Netmask_ object representing the address block.
   */
  new(cidr: string): Netmask;
}

declare var Netmask: NetmaskConstructor;
