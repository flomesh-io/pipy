# Configuration

Pipy configration includes definitions of pipelines. Each pipeline definition is started with
keyword _pipeline_ followed by a name, which can be either in form of `<ip>:<port>` for a
port listening pipeline, or an arbitrary name for an internally referenced pipeline. After that
comes a series of module definitions. Each module definition includes the name of the module and,
optionally, its parameters. Each parameter is written as `<name> = <value>`.

```
# Start of Pipy configuration
pipy

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

The cascading in Pipy configurations is determined by indentation. Pipy configuration files
always start with word _pipy_, taking up the entire first line with no indentation, followed
by pipeline definitions with one level deeper indentation. Module definitions should be
indented deeper than pipelines. Module parameters should be indented deeper than their modules.

Comments are started with `#`. They can appear at the end of a line, or take up its own line.

For example, a pipeline that says 'Hey, wassup!' to any HTTP requests would be something
like this:

```
pipy
  pipeline :6000
    decode-http-request
    hello
      message = Hey, wassup!\n
    encode-http-response
```
