(config =>

pipy({
  _services: config.services,
  _service: null,

  _obj2xml: node => (
    typeof node === 'object' ? (
      Object.entries(node).flatMap(
        ([k, v]) => (
          v instanceof Array && (
            v.map(e => new XML.Node(k, null, _obj2xml(e)))
          ) ||
          v instanceof Object && (
            new XML.Node(k, null, _obj2xml(v))
          ) || (
            new XML.Node(k, null, [v])
          )
        )
      )
    ) : [
      new XML.Node('body', null, [''])
    ]
  ),

  _xml2obj: node => (
    node ? (
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
          k = node.name,
          v = children.length != 1 || first instanceof XML.Node ? _xml2obj(node) : (first || ''),
          previous = obj[k],
          previous instanceof Array ? (
            previous.push(v)
          ) : (
            obj[k] = previous ? [previous, v] : v
          )
        )),
        obj
      ))()
    ) : {}
  ),
})

.import({
  __serviceID: 'proxy',
})

.pipeline('input')
  .handleSessionStart(
    () => _service = _services[__serviceID]
  )
  .link(
    'json2xml', () => Boolean(_service?.jsonToXml),
    'xml2json', () => Boolean(_service?.xmlToJson),
    'bypass'
  )

.pipeline('json2xml')
  .replaceMessageBody(
    data => (
      XML.encode(
        new XML.Node(
          '', null, [
            new XML.Node(
              'root', null,
              _obj2xml(JSON.decode(data))
            )
          ]
        ),
        2,
      )
    )
  )

.pipeline('xml2json')
  .replaceMessageBody(
    data => (
      JSON.encode(
        _xml2obj(XML.decode(data))
      )
    )
  )

.pipeline('bypass')

)(JSON.decode(pipy.load('transform.json')))