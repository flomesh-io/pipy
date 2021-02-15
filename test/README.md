# Tests and Samples

## 001 echo

This test shows how HTTP messages can be processed by using _decode-http-request_ and
_encode-http-response_ and how the two modules can be connected by sharing the same
context prefix so that responses can behave in accord with requests.

## 002 hello

This test demonstrates the usage of _hello_ for generating a simple text message.

## 003 json

This test demonstrates how a JSON message can be decoded into or encoded from a stream
of JSON values. It also shows how we can transform streamed JSON on the fly. By combining
modules _filter_ and _replace_, a JSON document can be easily swapped right in the stream.

## 004 js-echo

This test adds on top of _001-echo_ an empty JavaScript _script_ module. It shows
the overhead we are expecting from using QuickJS scripting. Based on our tests, using
an empty JS module introduces about 10% lower TPS and a 1.5MB extra memory usage.

## 005 js-hello

This test demonstrates how the same job as in _002-hello_ can be done with a JavaScript
module instead of the C++ module _hello_. Comparisons can be done against _002-hello_
to see how much an overhead we are to look at as to TPS, latency and memory usage.

## 006 js-json

This test demonstrates how to write a JavaScript stream handler. The handler in this demo
transforms a stream of JSON values into a tree structured JSON object.

## 007 js-json-parse

This test demonstrates a JavaScript stream handler that reads up the entire message body
before parsing it as a JSON document. While streaming processing can be hard to code, often
times we can just make things much easier if we only deal with the message as a whole like
in this demo.

## 008 js-crypto

This test demonstrates the use of builtin SM2 signature and SM4 encryption algorithms. It
serves at four endpoints for different function tests including:

* _/sign_ Returns the signature of a posted message
* _/veri_ Verifies the signature of a posted message as in HTTP header *sig*
* _/enc_ Encrypts a posted message
* _/dec_ Decrypts a posted message

## 009 routing

This test shows how HTTP requests can be routed to different upstreams by their URIs.
Although the demo shown here maps URIs to target hosts merely by using a JavaScript
hashtable, a real life use case can be however complex in the routing logic thanks to
the flexibility of scripting.

## 010 load-balancing

This test shows how a simple round-robin load balancing proxy can be implemented with
just a few lines of JavaScript.

## 011 serve-static

This test demonstrates how easy it could be to setup a simple static file server by using
_serve-static_ module. 

## 012 logging

This test demonstrates how details about requests and responses can be spit out to
a log storage, like ElasticSearch, on a shared long-lived connection.

## 013 metrics

This test shows how key metrics can be collected and exposed in Prometheus format by
using _count_ and _prometheus_ modules.

## 014 throttle

This test makes use of a `tap` module for limiting the rate of incoming requests to under
100 requests per second, which can be useful when upstream services need protection from
high traffic volumes.

## 015 dubbo

This test demonstrates how Pipy can be used to inspect Dubbo traffic and act as a
REST-to-Dubbo converter where Dubbo providers are exposed as RESTful services speaking JSON.

## 016 xml

This test shows how XML documents can be transformed into JSON after being decoded first as
XML and then encoded as JSON while they pass through XML/JSON codec modules.

# Translations

## [中文版](https://github.com/flomesh-io/pipy/blob/main/test/README_zh.md)
