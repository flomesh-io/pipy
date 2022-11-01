export default function({ attack }) {

  function verify(data, count, targets) {
    const lines = data.toString().split('\n').filter(l => Boolean(l));
    if (lines.length != count) {
      throw new Error(`Expected ${count} responses but got ${lines.length}`);
    }
    const counts = Object.fromEntries(
      Object.keys(targets).map(k => [k, 0])
    );
    lines.forEach(
      (line, i) => {
        if (line in counts) {
          counts[line]++;
        } else {
          throw new Error(`Unexpected response at line ${i}: ${line}`);
        }
      }
    );
    const total = Object.values(targets).reduce((a, b) => a + b);
    const expected = Object.fromEntries(
      Object.entries(targets).map(
        ([k, v]) => [k, count * v / total]
      )
    );
    for (const k in counts) {
      if (Math.abs(counts[k] - expected[k]) > 1) {
        throw new Error(`Unexpected target hits ${JSON.stringify(counts)}`);
      }
    }
  }

  attack(
    0,
    new Array(100).fill(
      [
        'GET /foo HTTP/1.1\r\n\r\n',
        'POST /foo HTTP/1.1\r\n',
        'content-length: 6\r\n\r\n',
        'Hello!',
      ]
    ).flat(),
    data => verify(data, 200, { '8080': 2, '8081': 1, '8082': 1 }),
  );

  attack(
    0,
    new Array(100).fill(
      [
        'POST /bar HTTP/1.1\r\n',
        'content-length: 6\r\n\r\n',
        'Hello!',
        'GET /bar HTTP/1.1\r\n\r\n',
      ]
    ).flat(),
    data => verify(data, 200, { '8088': 2, '8089': 8 }),
  );
}
