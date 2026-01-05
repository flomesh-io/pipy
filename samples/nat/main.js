#!/usr/bin/env pipy

import { discoverPublicAddress } from './nat.js'

var config = YAML.decode(pipy.load('config.yaml'))

println('NAT Traversal Sample')
println('====================')
println('')

// Discover public address via STUN
println('Discovering public address via STUN...')
println('STUN Server:', config.stunServer + ':' + config.stunPort)
println('')

discoverPublicAddress(config.stunServer, config.stunPort).then(
    function (result) {
        println('Public Address Discovered:')
        println('  IP:     ', result.ip)
        println('  Port:   ', result.port)
        println('  Family: ', result.family)
        println('')

        // Set up UDP echo server for testing
        println('Starting UDP echo server on port', config.listenPort)
        println('Send UDP packets to test connectivity')
        println('')

        pipy.listen(config.listenPort, 'udp', $ => $
            .handleData(
                function (data) {
                    var msg = data.toString()
                    println('[Received]', msg)

                    // Echo back
                    if (msg.indexOf('PING') >= 0) {
                        return new Data('PONG')
                    }
                    return data
                }
            )
        )
    }
).catch(
    function (err) {
        println('Error:', err)
    }
)
