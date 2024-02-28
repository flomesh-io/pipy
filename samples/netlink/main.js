#!/usr/bin/env pipy

import rtnl from './rtnl.js'

rtnl(
  function (type, obj) {
    println(type, obj)
  }
)
