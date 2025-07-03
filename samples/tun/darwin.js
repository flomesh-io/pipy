var UTUN_CONTROL_NAME = 'com.apple.net.utun_control'

export default function(ip) {

  // struct sockaddr_ctl (sys/kern_control.h)
  var sockaddr_ctl = new CStruct({
    len: 'uint8',
    family: 'uint8',
    sysaddr: 'uint16',
    id: 'uint32',
    unit: 'uint32',
    reserved: 'uint32[5]',
  })

  // struct ctl_info (sys/kern_control.h)
  var ctl_info = new CStruct({
    id: 'uint32',
    name: 'char[96]',
  })

  var str_buf = new CStruct({
    str: 'char[256]'
  })

  var $ctlID
  var $ifName

  pipeline($=>$
    .onStart(new Data)
    .connect(
      () => sockaddr_ctl.encode({
        len: sockaddr_ctl.size,
        family: 32, // AF_SYSTEM
        sysaddr: 2, // AF_SYS_CONTROL
        id: $ctlID,
        unit: 0,
      }), {
        domain: 32, // PF_SYSTEM
        type: 'datagram',
        protocol: 2, // SYSPROTO_CONTROL
        onState: ob => {
          switch (ob.state) {
          case 'open':
            var out = new Data
            if (!ob.socket.ioctl(
              0xc0644e03, // CTLIOCGINFO
              ctl_info.encode({
                name: UTUN_CONTROL_NAME
              }), out)
            ) {
              $ctlID = ctl_info.decode(out).id
            } else {
              println('Cannot find utun control:', UTUN_CONTROL_NAME)
            }
            break
          case 'connected':
            var out = new Data
            if (!ob.socket.getRawOption(
              2, // SYSPROTO_CONTROL
              2, // UTUN_OPT_IFNAME
              out
            )) {
              $ifName = str_buf.decode(out).str
              println('Created utun interface:', $ifName)
              pipy.exec(['ifconfig', $ifName, ip, ip, 'mtu', 1500, 'up'])
            } else {
              println('Cannot get utun interface name')
            }
            break
          }
        }
      }
    )
    .dump()
  ).spawn()
}
