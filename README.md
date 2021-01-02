# pipy
Pipy is a lightweight proxy primary for sidecar-proxy usage. It's lightweight, in the same wrk test, pipy use about 1/5 memory(4M/20M) and provider about 110% TPS compare to Nginx while proxying HTTP1.1 requests. And use about 1/6 memory and provides 130% TPS compare to Envoy. Pipy is written in C++, with quickjs for embedded script engine, pipy uses asio(preactor pattern) for event. 

It's a component of Flomesh (a service mesh), source code is being refactoring for open source, coming soon.
