export default function({ attack, reload }) {

  const expected = [
    '8080',
    '8081',
    '8082' + 'X'.repeat(10000),
  ];

  function verify(data, count) {
    const lines = data.toString().split('\n').filter(l => Boolean(l));
    if (lines.length != count) {
      throw new Error(`Expected ${count} responses but got ${lines.length}`);
    }
    lines.forEach(
      (line, i) => {
        if (line !== expected[i % 3]) {
          throw new Error(`Unexpected response at line ${i}: ${line}`);
        }
      }
    );
  }

  attack(
    0,
    new Array(100).fill(
      [
        'GET /foo HTTP/1.1\r\n\r\n',
        'GET /bar HTTP/1.1\r\n\r\n',
        'GET /xyz HTTP/1.1\r\n\r\n',
      ]
    ).flat(),
    data => verify(data, 300),
  );

  attack(
    10,
    new Array(50).fill(
      [
        'GET /foo ', 'HTTP/1.1\r\n\r\n',
        'GET /bar ', 'HTTP/1.1\r\n\r\n',
        'GET /xyz ', 'HTTP/1.1\r\n\r\n',
      ]
    ).flat(),
    data => verify(data, 150),
  );

  attack(
    100,
    new Array(30).fill(
      [
        'GET /foo ', 'HTTP/1.1\r\n\r\n',
        'GET /bar ', 'HTTP/1.1\r\n\r\n',
        'POST /xyz ', 'HTTP/1.1\r\n',
        'ConTent-lengTH: 3000\r\n\r\n',
        'X'.repeat(1000),
        'Y'.repeat(1000),
        'Z'.repeat(1000),
      ]
    ).flat(),
    data => verify(data, 90),
  );

  for (let i = 0; i < 10; i++) {
    attack(
      i * 20,
      new Array(30).fill(
        [
          'POST /foo HTTP/1.1\r\n',
          'content-length: 6\r\n\r\n',
          'Hello!',
          'POST /bar HTTP/1.1\r\n',
          'content-length: 6\r\n\r\n',
          'Hello!',
          'POST /xyz HTTP/1.1\r\n',
          'content-length: 6\r\n\r\n',
          'Hello!',
        ]
      ).flat(),
      data => verify(data, 90),
    );
  }

  reload(100);
  reload(200);
  reload(300);
}
