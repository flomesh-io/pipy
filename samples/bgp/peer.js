(config, address) => (
  (
    MY_AS = config.as,
    BGP_IDENTIFIER = config.id,
    NEXT_HOP = config.nextHop,
    HOLD_TIME = (holdTime in config ? config.holdTime : 90),

    state = 'Idle',
    holdTimer = -1,
    holdTime = -1,
    keepaliveTimer = -1,
    keepaliveTime = -1,
    peerAS = -1,
    updateScheduled = false,

    ipv6 = (ip) => (
      (
        m = new Netmask(ip)
      ) => ({
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
      })
    )(),

    ipv6Prefix = (ip) => (
      ({mask, data} = ipv6(ip)) => [
        mask, ...data.slice(0, Math.ceil(mask / 8))
      ]
    )(),

    //
    // State machine event handling
    //

    onEvent = (evt, msg) => (
      select(state,
        'Idle', () => stateIdle(evt, msg),
        'OpenSent', () => stateOpenSent(evt, msg),
        'OpenConfirm', () => stateOpenConfirm(evt, msg),
        'Established', () => stateEstablished(evt, msg),
      )
    ),

    stateIdle = (evt) => (
      select(evt,
        'ManualStart', () => (
          holdTimer = 4*60,
          state = 'OpenSent',
          new Message(
            null, {
              type: 'OPEN',
              body: {
                myAS: MY_AS,
                identifier: BGP_IDENTIFIER,
                holdTime: HOLD_TIME,
                parameters: {
                  Capabilities: {
                    // Multiprotocol Extensions
                    '1': config.isIPv6 ? [
                      new Data([0, 1, 0, 1]), // IPv4 unicast
                      new Data([0, 2, 0, 1]), // IPv6 unicast
                    ] : undefined,

                    // Route Refresh
                    '2': null,

                    // Support for 4-octet AS number
                    '65': MY_AS > 0xffff ? new Data([
                      255 & (MY_AS >> 24),
                      255 & (MY_AS >> 16),
                      255 & (MY_AS >>  8),
                      255 & (MY_AS >>  0),
                    ]) : undefined,
                  },
                },
              }
            }
          )
        )
      )
    ),

    stateOpenSent = (evt, msg) => (
      select(evt,
        'BGPOpen', () => void (
          verifyOpen(msg) || (
            holdTimer = holdTime = Math.min(
              HOLD_TIME,
              msg.body.holdTime,
            ) || -1,
            keepaliveTime = holdTime > 0 ? Math.floor(holdTime/3) : -1,
            keepaliveTimer = 0, // triggers a keepalive
            updateScheduled = true,
            state = 'OpenConfirm'
          )
        ),
        'HoldTimer_Expires', () => (
          holdTimerExpired()
        )
      )
    ),

    stateOpenConfirm = (evt) => (
      select(evt,
        'KeepAliveMsg', () => void (
          holdTimer = holdTime,
          state = 'Established'
        ),
        'HoldTimer_Expires', () => (
          holdTimerExpired()
        ),
        'KeepaliveTimer_Expires', () => (
          keepaliveTimerExpired()
        ),
      )
    ),

    stateEstablished = (evt) => (
      select(evt,
        'ManualUpdate', () => (
          updateScheduled = false,
          keepaliveTimer = keepaliveTime,
          composeUpdate()
        ),
        'KeepAliveMsg', () => void (
          holdTimer = holdTime
        ),
        'HoldTimer_Expires', () => (
          holdTimerExpired()
        ),
        'KeepaliveTimer_Expires', () => (
          keepaliveTimerExpired()
        ),
      )
    ),

    verifyOpen = (msg) => void (
      peerAS = msg.body.myAS
    ),

    composeUpdate = () => (
      new Message(
        null, {
          type: 'UPDATE',
          body: {
            pathAttributes: [
              {
                name: 'ORIGIN',
                value: peerAS === MY_AS ? 0 : 1,
                transitive: true,
              },
              ...(peerAS === MY_AS ? [
                {
                  name: 'AS_PATH',
                  value: [],
                  transitive: true,
                },
                {
                  name: 'LOCAL_PREF',
                  value: peerAS === MY_AS ? 0 : 1,
                  transitive: true,
                },
              ] : [
                {
                  name: 'AS_PATH',
                  value: [[ MY_AS ]],
                  transitive: true,
                },
              ]),
              ...(config.isIPv6 ? [
                {
                  code: 14, // MP_REACH_NLRI
                  value: new Data([
                    // IPv6 unicast
                    0, 2, 1,
                    // Next Hop
                    16, ...ipv6(NEXT_HOP).data,
                    // No SNPAs
                    0,
                    // Destinations
                    ...config.prefixes.flatMap(
                      prefix => ipv6Prefix(prefix)
                    ),
                  ])
                }
              ] : [
                {
                  name: 'NEXT_HOP',
                  value: NEXT_HOP,
                  transitive: true,
                },
              ])
            ],
            destinations: config.isIPv6 ? [] : config.prefixes,
          }
        }
      )
    ),

    holdTimerExpired = () => (
      resetStateMachine(),
      new Message(
        null, {
          type: 'NOTIFICATION',
          body: {
            errorCode: 4, // Hold Timer Expired
          }
        }
      )
    ),

    keepaliveTimerExpired = () => (
      keepaliveTimer = keepaliveTime,
      new Message(
        null, {
          type: 'KEEPALIVE',
        }
      )
    ),

    resetStateMachine = () => (
      holdTimer = -1,
      keepaliveTimer = -1,
      state = 'Idle'
    ),

  ) => ({

    destination: `${address}:179`,

    state: () => state,

    //
    // Run once every second
    //

    tick: () => (
      state === 'Idle' ? (
        onEvent('ManualStart')
      ) : (
        branch(
          holdTimer-- === 0, () => onEvent('HoldTimer_Expires'),
          keepaliveTimer-- === 0, () => onEvent('KeepaliveTimer_Expires'),
          updateScheduled, () => onEvent('ManualUpdate'),
        )
      )
    ),

    //
    // Receive a message from the peer
    //

    receive: msg => void (
      select(msg.payload.type,
        'OPEN', () => onEvent('BGPOpen', msg.payload),
        'UPDATE', () => onEvent('UpdateMsg', msg.payload),
        'NOTIFICATION', () => onEvent('NotifMsg', msg.payload),
        'KEEPALIVE', () => onEvent('KeepAliveMsg', msg.payload),
      )
    ),

    //
    // Schedule an update
    //

    update: () => void (
      updateScheduled = true
    ),

    //
    // Disconnect from the peer
    //

    end: () => void (
      resetStateMachine()
    ),

  })
)()
