export default function (argv, cb) {
  var pos = 0
  argv.forEach(opt => {
    if (opt.startsWith('-')) {
      var i = opt.indexOf('=')
      if (i > 0) {
        cb(opt.substring(0, i), opt.substring(i + 1))
      } else {
        cb(opt, true)
      }
    } else {
      cb(pos++, opt)
    }
  })
}
