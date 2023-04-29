export default function({ attack, http, split }) {

  function makeVerifier(targets) {
    const total = Object.values(targets).reduce((a, b) => a + b);
    const expected = Object.fromEntries(
      Object.entries(targets).map(
        ([k, v]) => [k, v / total]
      )
    );
    const counts = Object.fromEntries(
      Object.keys(targets).map(k => [k, 0])
    );
    let count = 0;
    return function (msg, i) {
      const body = msg.body;
      if (body in counts) {
        counts[body]++;
        count++;
      } else {
        throw new Error(`Unexpected body in response ${i}: ${body}`);
      }
      if (count > total) {
        for (const k in counts) {
          if (Math.abs(counts[k] - count * expected[k]) > 3) {
            throw new Error(`Unexpected target hits ${JSON.stringify(counts)}`);
          }
        }
      }
    }
  }

  attack({
    messages: [].concat.apply([], new Array(100).fill(
      [
        http('GET', '/foo'),
        split(3, http('POST', '/foo', 'Hello!')),
      ]
    )),
    verify: makeVerifier({ '8080': 2, '8081': 1, '8082': 1 }),
  });

  attack({
    messages: [].concat.apply([], new Array(100).fill(
      [
        split(3, http('POST', '/bar', 'Hello!')),
        http('GET', '/bar'),
      ]
    )),
    verify: makeVerifier({ '8088': 2, '8089': 8 }),
  });
}
