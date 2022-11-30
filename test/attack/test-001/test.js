export default function({ attack, http, split, reload }) {

  const expected = [
    '8080',
    '8081',
    '8082' + 'X'.repeat(10000),
  ];

  function verify(msg, i) {
    if (msg.status !== 200) {
      throw new Error(`Unexpected status code ${msg.status}`);
    }
    if (msg.body !== expected[i % 3]) {
      throw new Error(`Unexpected body in response ${i}: ${msg.body}`);
    }
  }

  attack({
    delay: 0,
    messages: [].concat.apply([], new Array(100).fill(
      [
        http('GET', '/foo'),
        http('GET', '/bar'),
        http('GET', '/xyz'),
      ]
    )),
    verify,
  });

  attack({
    delay: 10,
    messages: [].concat.apply([], new Array(50).fill(
      [
        split(2, http('GET', '/foo')),
        split(2, http('GET', '/bar')),
        split(2, http('GET', '/xyz')),
      ]
    )),
    verify,
  });

  attack({
    delay: 100,
    messages: [].concat.apply([], new Array(30).fill(
      [
        split(2, http('GET', '/foo')),
        split(2, http('GET', '/bar')),
        split(6, http('POST', '/xyz', 'X'.repeat(3000))),
      ]
    )),
    verify,
  });

  for (let i = 0; i < 10; i++) {
    attack({
      delay: i * 20,
      messages: [].concat.apply([], new Array(30).fill(
        [
          split(3, http('POST', '/foo', 'Hello!')),
          split(3, http('POST', '/bar', 'Hello!')),
          split(3, http('POST', '/xyz', 'Hello!')),
        ]
      )),
      verify,
    });
  }

  for (let i = 1; i <= 10; i++) reload(i * 50);
}
