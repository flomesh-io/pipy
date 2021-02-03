# Pipy

Pipy is a cloud-native high-performance network traffic processor. Written
in C++, built on top of Asio asynchronous I/O library, Pipy is extremely
lightweight and fast, making it one of the best choices for service mesh sidecars.

With builtin JavaScript support, thanks to QuickJS, Pipy is highly
customizable and also predictable in performance with no garbage collection
burden found in other scriptable counterparts.

At its core, Pipy has a modular design with many small reusable modules
that can be chained together to make a pipeline, through which network data
flow and get processed on the way. The way Pipy is designed makes it versatile
enough for not only sidecars but also other use cases involving intermediate
message processing between network nodes.

# How to Build

Before building, the following tools are required to be installed first:

* Clang
* CMake 2.8+

The build process is as simple as this:

```bash
export CC=clang
export CXX=clang++
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
```

The executable is located at `bin/pipy`. Type `bin/pipy -h` for usage information.

# How to run 

## CLI
~~~
pipy test/001-echo/pipy.cfg --reuse-port --watch-config-file &
~~~

## K8S

[Follow this link to run pipy on k8s as CRD.](https://github.com/flomesh-io/pipy-operator/blob/main/README.md#quickstart)

# Terminology

## Stream

In Pipy, data streams are made up by objects rather than bytes. As raw network bytes
first hit Pipy, they are packed in chunks, each of which is called a _data object_.
As these data objects flow through Pipy, they are converted into other types of objects
at different stages. These objects are eventually all converted back to data objects
before being sent out onto the network again.

Besides data objects, other types of objects are:

* SessionStart / SessionEnd
* MessageStart / MessageEnd
* ListStart / ListEnd
* MapStart / MapKey / MapEnd
* NullValue / BoolValue / IntValue / LongValue / DoubleValue / StringValue

## Pipeline

A pipeline defines a series of operations being taken one after another on streams that
flow through. Each operation is carried out by a specific module.

Pipelines are named. When the name is in form of `<ip>:<port>`, the input to it
will be coming from that TCP listening port, and the output of the pipeline will be
sent back to the remote peer. That means, a port listening pipeline, when it contains
no modules at all, will be merely a simple TCP echo server.

When the name is not a listening port, the pipeline can be referenced inside Pipy,
and streams can be sent to it for processing without need of going on a physical network.

## Module

A module sits at some point in a pipeline, getting input objects from the previous
module, performing a certain kind of operation to the objects, and then spitting output
objects to the next module.

Different modules respond to different sets of input object types. For instance,
`decode-http-request` module takes in only data objects, figures out where HTTP messages
start and end, and spits out for each HTTP message a _MessageStart_, message body (in
data objects), _MessageEnd_ sequence. For another example, `encode-http-response` module
watches for _MessageStart_ and _MessageEnd_ objects, wraps up whatever is in between as
a valid HTTP message by putting a standard HTTP message header ahead, outputs the raw
byte representation of that HTTP message as data objects.

## Session

While pipelines define what to do to the streams passing by, they don't have a runtime
state, called `context`, associated to them. Sessions have that.

Sessions have their contexts created when incoming connections first open, and destroyed
when the connections close. When a session calls up another session inside Pipy by refering
to a different pipeline, the new session can inherit the context from its parent session, or
create its own new context.

## Context

A context is a bag of key-value pairs used as variables. It's associated with one or more
sessions and used by modules to share information among each other.

Variable names started with double underlines are reserved for pre-defined variables including:

* `__remote_addr`
* `__remote_port`
* `__local_addr`
* `__local_port`

Variables can be used for some of the module parameters as a way to give these modules
dynamic parameters that change across sessions. Enclose the variable name within `${...}` when
using them in an expression. For example, `"Connection from ${__remote_addr}"` could be evaluated
into string `"Connection from 127.0.0.1"` when the connection is from _localhost_.

Contexts also include named `queues` that link between modules for sending objects
from one module to another. Queues are useful in cases where a module starts sending
objects before any other modules start to receive. A queue will buffer up all the produced
objects when they are not consumed yet.

# Configuration

Pipy configration includes definitions of pipelines. Each pipeline definition is started with
keyword _pipeline_ followed by a name, which can be either in form of `<ip>:<port>` for a
port listening pipeline, or an arbitrary name for an internally referenced pipeline. After that
comes a series of module definitions. Each module definition includes the name of the module and,
optionally, its parameters. Each parameter is written as `<name> = <value>`.

```
# First pipeline
pipeline <name>
  <module>
    <parameter> = <value>
    <parameter> = <value>
    ...
  <module>
    <parameter> = <value>
    <parameter> = <value>
    ...
  ...

# Second pipeline
pipeline <name>
  <module>
    <parameter> = <value>
    <parameter> = <value>
    ...
  ...

# More pipelines if needed
...

```

The cascading in Pipy configurations is determined by indentation. Pipeline definitions
are on the top with no indentations. Module definitions should be indented deeper than
pipelines. Module parameters should be indented deeper than their modules.

Lines started with `#` are comments. Line trailing comments are not supported.

For example, a pipeline that says 'Hey, wassup!' to any HTTP requests would be something
like this:

```
pipeline 0.0.0.0:8080
  decode-http-request
  hello
    message = Hey, wassup!\n
  encode-http-response
```
