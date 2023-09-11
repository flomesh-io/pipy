interface Inbound {
  id: number;
  localAddress: string;
  localPort: number;
  remoteAddress: string;
  remotePort: number;
  destinationAddress: string;
  destinationPort: number;
}
