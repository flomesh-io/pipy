## BGP Speaker

Border Gateway Protocol (BGP) is a TCP based network routing protocol, which is commonly used for routing exchange between different Autonomous Systems (AS). BGP is responsible for announcing a set of IP addresses or prefixes to other routers on the network, and routers will select the best path to deliver packets to their destinations.

In this example, we will demonstrate how to use Pipy to implement BGP announcement. The component used for BGP announcement is called a BGP speaker.

##  Example Introduction

This example contains three files:

- [peer.js](./peer.js): This file defines BGP-related operations such as creating peers, encapsulating requests, and handling responses.
- [config.json](./config.json): This is the configuration file for the BGP speaker, and we will explain the configuration below.
- [main.js](./main.js): This is the core program that announces valid and invalid IP addresses/prefixes to peers (usually routers) every 1 second, and processes responses from peers.

### Configuration Explanation

- `as`: The identifier of the current AS.
- `id`: The IP address of the BGP speaker.
- `peers`: A set of router addresses for external announcements.
- `ipv4`: Announces IPv4 address information.
- `nextHop`: The address of the router that can access the announced IP.
- `reachable`: The list of IP addresses/prefixes to be announced, registered on peers.
- `unreachable`: The list of invalid IP addresses/prefixes to be removed from peers.
- `ipv6`: Announces IPv6 address information.

## Running

For information on how to run the program, please refer to our [blog](https://blog.flomesh.cn/implement-bgp-speaker-with-pipy/) for detailed instructions.
