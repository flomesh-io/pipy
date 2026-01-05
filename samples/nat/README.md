# Sample: NAT Traversal with STUN

This sample demonstrates NAT traversal using STUN (Session Traversal Utilities for NAT) to discover public IP addresses and ports behind NAT devices.

## What it does

- Discovers the public IP address and port via a STUN server
- Starts a UDP echo server for connectivity testing
- Demonstrates the STUN codec API for building NAT traversal applications

## Configuration

Edit `config.yaml` to configure:

- `stunServer`: STUN server address (default: stun.l.google.com)
- `stunPort`: STUN server port (default: 19302)
- `listenPort`: Local UDP port for echo server (default: 5000)

## Run the script

```sh
pipy main.js
```

Or, if `pipy` is visible in `$PATH`:

```sh
./main.js
```

## Test connectivity

After the script discovers your public address, test the UDP echo server:

```sh
# From another terminal
echo "PING" | nc -u localhost 5000
```

## NAT Library

The `nat.js` library provides three functions:

- `discoverPublicAddress(stunServer, stunPort)` - Discover public IP/port via STUN
- `testConnectivity(peerIp, peerPort)` - Test UDP connectivity to a peer
- `connectP2P(...)` - Establish P2P connection with UDP hole punching

## STUN Codec API

The STUN codec is available as a global API:

```js
// Encode STUN Binding Request
var tid = new Data([...]) // 12-byte transaction ID
var request = STUN.encode({ type: 'BindingRequest', transactionId: tid })

// Decode STUN Binding Response
var response = STUN.decode(data)
// response = { type, transactionId, mappedAddress: { ip, port, family } }
```
