/**
 * Configuration is used to set up context variables and pipeline layouts in a module.
 */
declare class Configuration {

  /**
   * Defines context variables that are accessible to other modules.
   */
  export(namespace: string, variables: { [key: string]: any }): Configuration;

  /**
   * Imports context variables defined and exported from other modules.
   */
  import(variables: { [key: string]: string }): Configuration;

  /**
   * Creates a pipeline layout for incoming TCP/UDP connections on a port.
   */
  listen(port: number | null, options?: {}): Configuration;

  /**
   * Creates a pipeline layout for reading from a file.
   */
  read(filename: string): Configuration;

  /**
   * Creates a pipeline layout for a periodic job or a signal. 
   */
  task(intervalOrSignal?: number | string): Configuration;

  /**
   * Creates a sub-pipeline layout.
   */
  pipeline(name?: string): Configuration;

  /**
   * Registers a function to be called when a pipeline is created.
   */
  onStart(handler: () => Event | Message | (Event|Message)[]): Configuration;

  /**
   * Registers a function to be called when a pipeline is destroyed.
   */
  onEnd(handler: () => void): Configuration;

  /**
   * Attaches a sub-pipeline layout to the last joint filter.
   */
  to(nameOrBuilder: string | ((pipelineConfigurator: Configuration) => void)): Configuration;

  /**
   * Appends an _acceptHTTPTunnel_ filter to the current pipeline layout.
   *
   * An _acceptHTTPTunnel_ filter implements HTTP tunnel on the server side.
   * Its input and output are a _Message_ at the beginning and switch to _Data_ after the tunnel is established.
   * Its sub-pipeline's input and output are _Data_ streams.
   */
  acceptHTTPTunnel(handler: (request: Message) => Message): Configuration;
}
