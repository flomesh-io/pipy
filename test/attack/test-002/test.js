export default function({ attack, http, split, reload }) {

  const expected = [
    '8080',
    '8081',
    '8082' + 'X'.repeat(10000),
  ];

  function verify(line, i) {
    if (line !== expected[i % 3]) {
      throw new Error(`Unexpected response at line ${i}: ${line}`);
    }
  }

  attack({
    delay: 0,
    verify,
    messages: new Array(100).fill(
      [
        http('GET', '/foo'),
        http('GET', '/bar'),
        http('GET', '/xyz'),
      ]
    ).flat(),
  });

  attack({
    delay: 10,
    verify,
    messages: new Array(50).fill(
      [
        split(2, http('GET', '/foo')),
        split(2, http('GET', '/bar')),
        split(2, http('GET', '/xyz')),
      ]
    ).flat(),
  });

  attack({
    delay: 100,
    verify,
    messages: new Array(30).fill(
      [
        split(2, http('GET', '/foo')),
        split(2, http('GET', '/bar')),
        split(6, http('POST', '/xyz', 'X'.repeat(1000) + 'Y'.repeat(1000) + 'Z'.repeat(1000))),
      ]
    ).flat(),
  });

  for (let i = 0; i < 10; i++) {
    attack({
      delay: i * 20,
      verify,
      messages: new Array(30).fill(
        [
          split(3, http('POST', '/foo', 'Hello!')),
          split(3, http('POST', '/bar', 'Hello!')),
          split(3, http('POST', '/xyz', 'Hello!')),
        ]
      ).flat(),
    });
  }

  for (let i = 1; i <= 10; i++) reload(i * 50);
}
