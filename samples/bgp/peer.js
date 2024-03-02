export default function (config, address) {

  var AS_TRANS = 23456
  var MY_AS = config.as
  var BGP_IDENTIFIER = config.id
  var HOLD_TIME = (holdTime in config ? config.holdTime : 90)
  var IPV4_NEXT_HOP = os.env.BGP_SPEAKER_IPV4_NEXTHOP || config.ipv4.nextHop
  var IPV6_NEXT_HOP = os.env.BGP_SPEAKER_IPV6_NEXTHOP || config.ipv6.nextHop

  var state = 'Idle'
  var holdTimer = -1
  var holdTime = -1
  var keepaliveTimer = -1
  var keepaliveTime = -1
  var peerAS = -1
  var isEBGP = undefined
  var isAS4 = false
  var updateScheduled = false

  return {
    destination: `${address}:179`,
    state: () => state,
    isAS4: () => isAS4,
    tick,
    receive,
    update: () => { updateScheduled = true },
    reset: resetStateMachine,
  }

  //
  // Run once every second
  //

  function tick() {
    if (state === 'Idle') {
      return onEvent('ManualStart')
    } else if (holdTimer-- === 0) {
      return onEvent('HoldTimer_Expires')
    } else if (keepaliveTimer-- === 0) {
      return onEvent('KeepaliveTimer_Expires')
    } else if (updateScheduled) {
      return onEvent('ManualUpdate')
    }
  }

  //
  // Receive a message from the peer
  //

  function receive(msg) {
    switch (msg.payload.type) {
      case 'OPEN': onEvent('BGPOpen', msg.payload); break
      case 'UPDATE': onEvent('UpdateMsg', msg.payload); break
      case 'NOTIFICATION': onEvent('NotifMsg', msg.payload); break
      case 'KEEPALIVE': onEvent('KeepAliveMsg', msg.payload); break
    }
  }

  //
  // State machine event handling
  //

  function onEvent(evt, msg) {
    switch (state) {
      case 'Idle': return stateIdle(evt, msg)
      case 'OpenSent': return stateOpenSent(evt, msg)
      case 'OpenConfirm': return stateOpenConfirm(evt, msg)
      case 'Established': return stateEstablished(evt, msg)
    }
  }

  function stateIdle(evt) {
    switch (evt) {
      case 'ManualStart':
        holdTimer = 4*60
        state = 'OpenSent'
        return composeOpen()
    }
  }

  function stateOpenSent(evt, msg) {
    switch (evt) {
      case 'BGPOpen':
        if (!verifyOpen(msg)) {
          holdTimer = holdTime = Math.min(
            HOLD_TIME,
            msg.body.holdTime,
          ) || -1
          keepaliveTime = holdTime > 0 ? Math.floor(holdTime/3) : -1
          keepaliveTimer = 0 // triggers a keepalive
          updateScheduled = true
          state = 'OpenConfirm'
        }
        return
      case 'HoldTimer_Expires':
        return holdTimerExpired()
    }
  }

  function stateOpenConfirm(evt) {
    switch (evt) {
      case 'KeepAliveMsg':
        holdTimer = holdTime
        state = 'Established'
        return
      case 'HoldTimer_Expires':
        return holdTimerExpired()
      case 'KeepaliveTimer_Expires':
        return keepaliveTimerExpired()
    }
  }

  function stateEstablished(evt) {
    switch (evt) {
      case 'ManualUpdate':
        updateScheduled = false
        keepaliveTimer = keepaliveTime
        return composeUpdate()
      case 'KeepAliveMsg':
        holdTimer = holdTime
        return
      case 'HoldTimer_Expires':
        return holdTimerExpired()
      case 'KeepaliveTimer_Expires':
        return keepaliveTimerExpired()
    }
  }

  function verifyOpen(msg) {
    var as2 = msg.body.myAS
    var as4 = msg.body.parameters
      .filter(p => p.name === 'Capabilities')
      .flatMap(p => p.value)
      .find(cap => cap.code === 65)?.value?.toArray?.()
    peerAS = as2 !== AS_TRANS ? as2 : (
      (as4[0] << 24) |
      (as4[1] << 16) |
      (as4[2] <<  8) |
      (as4[3] <<  0)
    )>>>0
    isEBGP = peerAS !== MY_AS
    console.debug('Peer AS =', peerAS, isEBGP ? '(eBGP)' : '(iBGP)')
    isAS4 = msg.body.parameters.some(
      p => p.name === 'Capabilities' && (
        p.value.find(
          cap => cap.code === 65 // Support for 4-octet AS number
        )
      )
    )
  }

  function composeOpen() {
    return new Message(null, {
      type: 'OPEN',
      body: {
        myAS: MY_AS > 0xffff ? AS_TRANS : MY_AS,
        identifier: BGP_IDENTIFIER,
        holdTime: HOLD_TIME,
        parameters: [
          {
            name: 'Capabilities',
            value: [
              {
                // Multiprotocol Extensions: IPv4 unicast
                code: 1,
                value: new Data([0, 1, 0, 1]),
              },
              {
                // Multiprotocol Extensions: IPv6 unicast
                code: 1,
                value: new Data([0, 2, 0, 1]),
              },
              {
                // Graceful Restart Capability
                code: 64,
                value: new Data([
                  // Restart State + Restart Time
                  0x80 | (0x0f & (HOLD_TIME>>8)),
                  0x00 | (0xff & (HOLD_TIME>>0)),
                  // IPv4 unicast + Forwarding State
                  0, 1, 1, 0x80,
                  // IPv6 unicast + Forwarding State
                  0, 2, 1, 0x80,
                ]),
              },
              {
                // Support for 4-octet AS number
                code: 65,
                value: new Data([
                  255 & (MY_AS >> 24),
                  255 & (MY_AS >> 16),
                  255 & (MY_AS >>  8),
                  255 & (MY_AS >>  0),
                ]),
              },
            ]
          }
        ],
      }
    })
  }

  function composeUpdate() {
    var messages = []
    var hasIPv4 = config.ipv4.reachable.length > 0 || config.ipv4.unreachable.length > 0
    var hasIPv6 = config.ipv6.reachable.length > 0 || config.ipv6.unreachable.length > 0
    var commonPathAttrs = [
      {
        name: 'ORIGIN',
        value: isEBGP ? 1 : 0,
        transitive: true,
      },
      {
        name: 'AS_PATH',
        value: isEBGP ? [[MY_AS]] : [],
        transitive: true,
      },
      isEBGP ? undefined : {
        name: 'LOCAL_PREF',
        value: 0,
        transitive: true,
      },
    ]

    // Routes for IPv4 unicast
    if (hasIPv4) {
      messages.push(new Message(null, {
        type: 'UPDATE',
        body: {
          pathAttributes: [
            ...commonPathAttrs,
            {
              name: 'NEXT_HOP',
              value: IPV4_NEXT_HOP,
              transitive: true,
            },
          ],
          withdrawnRoutes: config.ipv4.unreachable,
          destinations: config.ipv4.reachable,
        }
      }))
    }

    // Routes for IPv6 unicast
    if (hasIPv6) {
      messages.push(new Message(null, {
        type: 'UPDATE',
        body: {
          pathAttributes: [
            ...commonPathAttrs,
            {
              // MP_REACH_NLRI
              code: 14,
              value: new Data([
                // IPv6 unicast
                0, 2, 1,
                // Next Hop
                16, ...ipv6(IPV6_NEXT_HOP).data,
                // No SNPAs
                0,
                // NLRI
                ...config.ipv6.reachable.flatMap(a => ipv6Prefix(a)),
              ]),
              optional: true,
            },
            {
              // MP_UNREACH_NLRI
              code: 15,
              value: new Data([
                // IPv6 unicast
                0, 2, 1,
                // NLRI
                ...config.ipv6.unreachable.flatMap(a => ipv6Prefix(a)),
              ]),
              optional: true,
            }
          ],
          withdrawnRoutes: [],
          destinations: [],
        }
      }))
    }

    // End-of-RIB for IPv4 unicast
    if (hasIPv4) {
      messages.push(new Message(null, {
        type: 'UPDATE',
        body: {},
      }))
    }

    // End-of-RIB for IPv6 unicast
    if (hasIPv6) {
      messages.push(new Message(null, {
        type: 'UPDATE',
        body: {
          pathAttributes: [
            {
              // MP_UNREACH_NLRI
              code: 15,
              value: new Data([
                // IPv6 unicast
                0, 2, 1,
              ]),
              optional: true,
            },
          ],
        }
      }))
    }

    return messages
  }

  function holdTimerExpired() {
    resetStateMachine()
    return new Message(null, {
      type: 'NOTIFICATION',
      body: {
        errorCode: 4, // Hold Timer Expired
      }
    })
  }

  function keepaliveTimerExpired() {
    keepaliveTimer = keepaliveTime
    return new Message(null, {
      type: 'KEEPALIVE',
    })
  }

  function resetStateMachine() {
    holdTimer = -1
    keepaliveTimer = -1
    state = 'Idle'
  }

  function ipv6(ip) {
    var m = new Netmask(ip)
    return {
      mask: m.version === 6 ? m.bitmask : m.bitmask + 96,
      data: m.version === 6 ? (
        m.decompose().flatMap(
          u16 => [
            255 & (u16 >> 8),
            255 & (u16 >> 0),
          ]
        )
      ) : (
        [
          0xff, 0xff, 0xff, 0xff,
          0x00, 0x00, 0x00, 0x00,
          0x00, 0x00, 0x00, 0x00,
          ...m.decompose()
        ]
      )
    }
  }

  function ipv6Prefix(ip) {
    var info = ipv6(ip)
    var mask = info.mask
    var data = info.data
    return [mask, ...data.slice(0, Math.ceil(mask / 8))]
  }
}
