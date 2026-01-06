// NAT Traversal Library
// Implements NAT traversal logic using STUN codec primitives

// Discover public address via STUN
export var discoverPublicAddress = function (stunServer, stunPort) {
    stunPort = stunPort || 3478

    return new Promise(function (resolve, reject) {
        // Generate random 12-byte transaction ID
        var transactionId = new Data(new Array(12).fill(0).map(() => Math.floor(Math.random() * 256)))
        var request = STUN.encode({ type: 'BindingRequest', transactionId: transactionId })

        if (!request) {
            reject('Failed to encode STUN request')
            return
        }

        var timeout = new Timeout(5)
        var responded = false

        pipeline($ => $
            .onStart(new Data)
            .replaceData(() => request)
            .connect(() => stunServer + ':' + stunPort, { protocol: 'udp' })
            .handleData(
                function (data) {
                    if (responded) return
                    responded = true
                    timeout.cancel()

                    try {
                        var response = STUN.decode(data)
                        if (response && response.type === 'BindingResponse' && response.mappedAddress) {
                            resolve(response.mappedAddress)
                        } else {
                            reject('Invalid STUN response')
                        }
                    } catch (e) {
                        reject('Failed to decode STUN response: ' + e.message)
                    }
                }
            )
        ).spawn()

        timeout.wait().then(
            function () {
                if (!responded) {
                    reject('STUN request timeout')
                }
            }
        )
    })
}

// Connect P2P with hole punching
export var connectP2P = function (peerPublicIp, peerPublicPort, peerPrivateIp, peerPrivatePort, localPort) {
    localPort = localPort || 0

    return new Promise(function (resolve, reject) {
        var timeout = new Timeout(10)
        var connected = false
        var punchMessage = new Data('PUNCH')

        // Create a single listening connection that will receive responses
        var receivedResponse = false
        pipeline($ => $
            .onStart(new Data)
            .connect(() => peerPublicIp + ':' + peerPublicPort, {
                protocol: 'udp',
                bind: localPort > 0 ? '0.0.0.0:' + localPort : undefined
            })
            .handleData(
                function (data) {
                    var str = data.toString()
                    if (!connected && str.indexOf('PUNCH') >= 0) {
                        connected = true
                        receivedResponse = true
                        timeout.cancel()
                        resolve({
                            ip: peerPublicIp,
                            port: peerPublicPort
                        })
                    }
                }
            )
        ).spawn()

        // If private IP is different, try it too (without binding to avoid port conflict)
        if (peerPrivateIp && peerPrivateIp !== peerPublicIp) {
            pipeline($ => $
                .onStart(new Data)
                .connect(() => peerPrivateIp + ':' + peerPrivatePort, {
                    protocol: 'udp'
                })
                .handleData(
                    function (data) {
                        var str = data.toString()
                        if (!connected && str.indexOf('PUNCH') >= 0) {
                            connected = true
                            receivedResponse = true
                            timeout.cancel()
                            resolve({
                                ip: peerPrivateIp,
                                port: peerPrivatePort
                            })
                        }
                    }
                )
            ).spawn()
        }

        // Keep sending punch messages
        var retryCount = 0
        var maxRetries = 20

        function sendPunch() {
            if (connected || retryCount >= maxRetries) return
            retryCount++

            // Send punch message to public endpoint
            pipeline($ => $
                .onStart(new Data)
                .replaceData(() => punchMessage)
                .connect(() => peerPublicIp + ':' + peerPublicPort, { protocol: 'udp' })
            ).spawn()

            // Send to private endpoint if different
            if (peerPrivateIp && peerPrivateIp !== peerPublicIp) {
                pipeline($ => $
                    .onStart(new Data)
                    .replaceData(() => punchMessage)
                    .connect(() => peerPrivateIp + ':' + peerPrivatePort, { protocol: 'udp' })
                ).spawn()
            }

            if (!connected && retryCount < maxRetries) {
                new Timeout(0.5).wait().then(sendPunch)
            }
        }

        // Start sending
        sendPunch()

        // Set overall timeout
        timeout.wait().then(
            function () {
                if (!connected) {
                    reject('Connection timeout')
                }
            }
        )
    })
}

// Test connectivity
export var testConnectivity = function (peerIp, peerPort) {
    return new Promise(function (resolve, reject) {
        var startTime = Date.now()
        var timeout = new Timeout(3)
        var responded = false

        var pingMessage = new Data('PING')

        pipeline($ => $
            .onStart(new Data)
            .replaceData(() => pingMessage)
            .connect(() => peerIp + ':' + peerPort, { protocol: 'udp' })
            .handleData(
                function (data) {
                    if (responded) return
                    responded = true
                    timeout.cancel()

                    var str = data.toString()
                    if (str.indexOf('PONG') >= 0) {
                        var latency = Date.now() - startTime
                        resolve({
                            reachable: true,
                            latency: latency,
                            packetLoss: 0
                        })
                    }
                }
            )
        ).spawn()

        timeout.wait().then(
            function () {
                if (!responded) {
                    resolve({
                        reachable: false,
                        latency: 0,
                        packetLoss: 1.0
                    })
                }
            }
        )
    })
}
