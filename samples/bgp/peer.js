(config, address) => (
  (
    AS_TRANS = 23456,
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
    isEBGP = undefined,
    isAS4 = false,
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
          composeOpen()
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
      peerAS = msg.body.myAS,
      isEBGP = peerAS !== MY_AS,
      isAS4 = msg.body.parameters.some(
        p => p.name === 'Capabilities' && (
          p.value.find(
            cap => cap.code === 65 // Support for 4-octet AS number
          )
        )
      )
    ),

    composeOpen = () => (
      new Message(
        null, {
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
        }
      )
    ),

    composeUpdate = () => [
      new Message(
        null, {
          type: 'UPDATE',
          body: {
            pathAttributes: [
              {
                name: 'ORIGIN',
                value: isEBGP ? 1 : 0,
                transitive: true,
              },
              ...(isEBGP ? [
                {
                  name: 'AS_PATH',
                  value: [[ MY_AS ]],
                  transitive: true,
                },
              ] : [
                {
                  name: 'AS_PATH',
                  value: [],
                  transitive: true,
                },
                {
                  name: 'LOCAL_PREF',
                  value: 0,
                  transitive: true,
                },
              ]),
              ...(config.isIPv6 ? [
                {
                  // MP_REACH_NLRI
                  code: 14,
                  value: new Data([
                    // IPv6 unicast
                    0, 2, 1,
                    // Next Hop
                    16, ...ipv6(NEXT_HOP).data,
                    // No SNPAs
                    0,
                    // NLRI
                    ...config.reachable.flatMap(a => ipv6Prefix(a)),
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
                    ...config.unreachable.flatMap(a => ipv6Prefix(a)),
                  ]),
                  optional: true,
                }
              ] : [
                {
                  name: 'NEXT_HOP',
                  value: NEXT_HOP,
                  transitive: true,
                },
              ])
            ],
            withdrawnRoutes: config.isIPv6 ? [] : config.unreachable,
            destinations: config.isIPv6 ? [] : config.reachable,
          }
        }
      ),

      // End-of-RIB for IPv4 unicast
      new Message(
        null, {
          type: 'UPDATE',
          body: {},
        }
      ),

      // End-of-RIB for IPv6 unicast
      new Message(
        null, {
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
        }
      ),
    ],

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
    isAS4: () => isAS4,

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
