/// <reference no-default-lib="true"/>

/**
 * IPv4 CIDR address block.
 */
interface Netmask {

  /**
   * Base address.
   */
  readonly base: string;

  /**
   * Subnet mask in dot-decimal notation.
   */
  readonly mask: string;

  /**
   * Subnet mask width.
   */
  readonly bitmask: number;

  /**
   * Host mask in dot-decimal notation.
   */
  readonly hostmask: string;

  /**
   * Broadcast address.
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
   * Check if an address belongs to the block.
   */
  contains(ip: string): boolean;

  /**
   * Allocates an address from the block.
   */
  next(): string;
}

interface NetmaskConstructor {

  /**
   * Creates an instance of _Netmask_.
   */
  new(cidr: string): Netmask;
}

declare var Netmask: NetmaskConstructor;
