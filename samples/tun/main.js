var TUN_IP = '1.1.1.1'

switch (os.platform) {
  case 'darwin':
    pipy.import('./darwin.js').default(TUN_IP)
    break
  default:
    println('Platform not supported')
    break
}
