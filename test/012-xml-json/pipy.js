pipy({
  _obj2xml: node => (
    Object.entries(node).flatMap(ent => (
      ((k, v) => (
        k = ent[0],
        v = ent[1],
        v instanceof Array && (
          v.map(e => new XML.Node(k, null, _obj2xml(e)))
        ) ||
        v instanceof Object && (
          new XML.Node(k, null, _obj2xml(v))
        ) || (
          new XML.Node(k, null, [ v ])
        )
      ))()
    ))
  ),

  _xml2obj: node => (
    ((
      children,
      first,
      previous,
      obj, k, v,
    ) => (
      obj = {},
      node.children.forEach(node => (
        children = node.children,
        first = children[0],
        v = children.length != 1 || first instanceof XML.Node ? _xml2obj(node) : (first || ''),
        k = node.name,
        previous = obj[k],
        previous instanceof Array ? (
          previous.push(v)
        ) : (
          obj[k] = previous ? [previous, v] : v
        )
      )),
      obj
    ))()
  ),
})

// XML -> JSON
.listen(6080)
  .decodeHttpRequest()
  .replaceMessageBody(
    data => (
      JSON.encode(
        _xml2obj(XML.decode(data)),
        null,
        2,
      )
    )
  )
  .encodeHttpResponse()

// JSON -> XML
.listen(6081)
  .decodeHttpRequest()
  .replaceMessageBody(
    data => (
      XML.encode(
        new XML.Node(
          '', null,
          _obj2xml(JSON.decode(data))
        ),
        2,
      )
    )
  )
  .encodeHttpResponse()