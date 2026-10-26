// Regression check for Netmask.next()/first()/last() CIDR semantics.
//
// RFC 3021 (IPv4 /31) and RFC 6164 (IPv6 /127) point-to-point links have two
// usable addresses and no network/broadcast address; /32 and /128 are a single
// address. Before the fix, next() returned nothing for /31 and /127 (the
// inverted mask equals the initial cursor) and walked past the block for /32
// and /128 (the inverted mask is 0, which the cursor never reaches).
//
// Run: ./bin/pipy test/netmask/next-check.js
// Expected: "SUMMARY: 24 passed, 0 failed" and exit status 0.

function collect(nm, limit) {
  var out = [];
  for (var i = 0; i < (limit || 64); i++) {
    var s = nm.next();
    if (!s || s === '') break;
    out.push(s);
  }
  return out;
}

var failed = 0;
var passed = 0;

function check(label, got, want) {
  var ok = JSON.stringify(got) === JSON.stringify(want);
  if (ok) {
    passed++;
    console.log('PASS ' + label + ' -> ' + JSON.stringify(got));
  } else {
    failed++;
    console.log('FAIL ' + label + ' -> got ' + JSON.stringify(got) +
      ', want ' + JSON.stringify(want));
  }
}

// IPv4: /32 yields 1 address, /31 yields 2 (RFC 3021), wider blocks keep
// excluding network and broadcast addresses.
check('192.0.2.7/32 next', collect(new Netmask('192.0.2.7/32')),
  ['192.0.2.7']);
check('192.0.2.6/31 next', collect(new Netmask('192.0.2.6/31')),
  ['192.0.2.6', '192.0.2.7']);
check('192.0.2.4/30 next', collect(new Netmask('192.0.2.4/30')),
  ['192.0.2.5', '192.0.2.6']);
check('192.0.2.0/29 next', collect(new Netmask('192.0.2.0/29')),
  ['192.0.2.1', '192.0.2.2', '192.0.2.3', '192.0.2.4', '192.0.2.5',
   '192.0.2.6']);
check('192.0.2.0/24 next (first 3)', collect(new Netmask('192.0.2.0/24'), 3),
  ['192.0.2.1', '192.0.2.2', '192.0.2.3']);

// IPv6: same rules, /128 yields 1 and /127 yields 2.
check('2001:db8::5/128 next', collect(new Netmask('2001:db8::5/128')),
  ['2001:db8::5']);
check('2001:db8::4/127 next', collect(new Netmask('2001:db8::4/127')),
  ['2001:db8::4', '2001:db8::5']);
check('2001:db8::/126 next', collect(new Netmask('2001:db8::/126')),
  ['2001:db8::1', '2001:db8::2']);

// first() must be derived from the base address; the last IPv6 group used to
// be read uninitialized, so first() of an IPv6 /128 could return garbage.
check('fd00::5/128 first', new Netmask('fd00::5/128').first, 'fd00::5');
check('fd00::4/127 contains(fd00::5)',
  new Netmask('fd00::4/127').contains('fd00::5'), true);
check('192.0.2.7/32 first', new Netmask('192.0.2.7/32').first,
  '192.0.2.7');
check('192.0.2.0/24 last', new Netmask('192.0.2.0/24').last,
  '192.0.2.254');

// first()/last() themselves must special-case the point-to-point and
// single-address prefixes: /31 and /127 have no reserved network/broadcast
// address (so last is base+1, not base), while /32 and /128 are one address.
check('192.0.2.6/31 first', new Netmask('192.0.2.6/31').first, '192.0.2.6');
check('192.0.2.6/31 last', new Netmask('192.0.2.6/31').last, '192.0.2.7');
check('192.0.2.7/32 last', new Netmask('192.0.2.7/32').last, '192.0.2.7');
check('2001:db8::4/127 first', new Netmask('2001:db8::4/127').first,
  '2001:db8::4');
check('2001:db8::4/127 last', new Netmask('2001:db8::4/127').last,
  '2001:db8::5');
check('2001:db8::5/128 last', new Netmask('2001:db8::5/128').last,
  '2001:db8::5');

// Invariant across the fixed prefixes: first() == next() #1 and
// last() == next() #count.
[['192.0.2.6/31', 2], ['192.0.2.7/32', 1],
 ['2001:db8::4/127', 2], ['2001:db8::5/128', 1]].forEach(function (c) {
  var nm = new Netmask(c[0]);
  var seq = collect(nm, 8);
  check(c[0] + ' first/last match sequence',
    [seq.length, nm.first, nm.last], [c[1], seq[0], seq[seq.length - 1]]);
});

// next() must terminate: a /32 and a /128 own exactly one address, so calling
// next() after the block is exhausted returns an empty string.
var nm32 = new Netmask('192.0.2.7/32');
nm32.next();
check('192.0.2.7/32 next after end', nm32.next(), '');
var nm128 = new Netmask('2001:db8::5/128');
nm128.next();
check('2001:db8::5/128 next after end', nm128.next(), '');

console.log('SUMMARY: ' + passed + ' passed, ' + failed + ' failed');
if (failed > 0) {
  throw new Error(failed + ' netmask check(s) failed');
}
