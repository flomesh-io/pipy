/*
 *  Copyright (c) 2019 by flomesh.io
 *
 *  Unless prior written consent has been obtained from the copyright
 *  owner, the following shall not be allowed.
 *
 *  1. The distribution of any source codes, header files, make files,
 *     or libraries of the software.
 *
 *  2. Disclosure of any source codes pertaining to the software to any
 *     additional parties.
 *
 *  3. Alteration or removal of any notices in or on the software or
 *     within the documentation included within the software.
 *
 *  ALL SOURCE CODE AS WELL AS ALL DOCUMENTATION INCLUDED WITH THIS
 *  SOFTWARE IS PROVIDED IN AN “AS IS” CONDITION, WITHOUT WARRANTY OF ANY
 *  KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 *  OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 *  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 *  CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 *  TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 *  SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

extern const char FILTERS_HELP[] = { R"***(

link(target[, when[, target2[, when2, ...]]])
Sends events to a different pipeline
target = <string> Name of the pipeline to send events to
when = <function> Callback function that returns true if a target should be chosen

fork(target[, initializers])
Sends copies of events to other pipelines
target = <string> Name of the pipeline to send event copies to
initializers = <array|function> Functions to initialize each pipeline

merge(target, key[, options])
Merges messages from different pipelines to a shared pipeline
target = <string> Name of the pipeline to send messages to
key = <function> Callback function that gives the ID of the shared pipeline
options = <object> Options including maxSessions

mux(target, key[, options])
Runs messages from different pipelines through a shared pipeline
target = <string> Name of the pipeline to send messages to
key = <string|function> Callback function that gives the ID of the shared pipeline
options = <object> Options including maxSessions

demux(target)
Sends messages to a different pipline with each one in its own pipeline and context
target = <string> Name of the pipeline to send messages to

decodeHTTPRequest()
Deframes an HTTP request message

decodeHTTPResponse([options])
Deframes an HTTP response message
options = <object> Options currently including only bodiless

encodeHTTPRequest()
Frames an HTTP request message

encodeHTTPResponse([options])
Frames an HTTP response message
options = <object> Options currently including only bodiless

demuxHTTP(target)
Deframes HTTP requests, sends each to a separate pipeline, and frames their responses
target = <string> Name of the pipeline to receive deframed requests

muxHTTP(target, key[, options])
Frames HTTP requests, send to a new or shared pipeline, and deframes its responses
target = <string> Name of the pipeline to receive framed requests
key = <number|string|function> Key of the shared pipeline
options = <object> Options including maxSessions

serveHTTP(handler)
Serves HTTP requests with a handler function
handler = <function> Function that returns a response message

connect(target[, options])
Sends data to a remote endpoint and receives data from it
target = <string|function> Remote endpoint in the form of `<ip>:<port>`
options = <object> Includes bufferLimit, retryCount, retryDelay

decompressMessage(algorithm)
Decompresses message bodies
algorithm = <string|function> One of the algorithms: inflate, ...

decompressHTTP([enable])
Decompresses HTTP message bodies based on Content-Encoding header
enable = <function> Returns true to decompress or false otherwise

dummy()
Eats up all events

dump([tag])
Outputs events to the standard output
tag = <string|function> Tag that is printed alongside of the events

print()
Outputs raw data to the standard output

wait(condition)
Buffers up events until a condition is fulfilled
condition = <function> Callback function that returns whether the condition is fulfilled

connectSOCKS(target, address)
Connects to a pipeline in SOCKS5
target = <string> Name of the pipeline
address = <string|function> Target address in host:port format

acceptSOCKS(target, onConnect)
Accepts a SOCKS connection
target = <string> Name of the pipeline that receives SOCKS connections
onConnect = <function> Callback function that receives address, port, user and returns whether the connection is accepted

connectTLS(target)
Connects to a pipeline with TLS ecryption
target = <string> Name of the pipeline to connect to

acceptTLS(target)
Accepts and TLS-offloads a TLS-encrypted stream
target = <string> Name of the pipeline to send TLS-offloaded stream to

pack([batchSize[, options]])
Packs data of one or more messages into one message and squeezes out spare room in the data chunks
batchSize = <int> Number of messages to pack in one. Defaults to 1
options = <object> Options including timeout, vacancy

split(callback)
Splits data chunks into smaller chunks
callback = <function> A callback function that receives each byte as input and decides where to split

split(callback)
Splits data chunks into smaller chunks
callback = <function> A callback function that receives each byte as input and decides where to split

throttleDataRate(quota[, account])
Throttles data rate
quota = <number|string|function> Quota in bytes/sec
account = <string|function> Name under which the quota is entitled to

throttleMessageRate(quota[, account])
Throttles message rate
quota = <number|string|function> Quota in messages/sec
account = <string|function> Name under which the quota is entitled to

exec(command)
Spawns a child process and connects to its input/output
command = <string|array|function> Command line to execute

decodeDubbo()
Deframes a Dubbo message

encodeDubbo([head])
Frames a Dubbo message
head = <object|function> Message head including id, status, isRequest, isTwoWay, isEvent

use(modules, pipeline[, pipelineDown[, turnDown]])
Sends events to pipelines in different modules
modules = <string|array> Filenames of the modules
pipeline = <string> Name of the pipeline
pipelineDown = <string> Name of the pipeline to process turned down streams
turnDown = <function> Callback function that returns true to turn down

handleMessageBody([sizeLimit, ]callback)
Handles a complete message body
sizeLimit = <number|string> Maximum number of bytes to collect from the message body
callback = <function> Callback function that receives a complete message body

handleData(callback)
Handles a Data event
callback = <function> Callback function that receives a Data event

handleMessageStart(callback)
Handles a MessageStart event
callback = <function> Callback function that receives a MessageStart event

handleMessageEnd(callback)
Handles a MessageEnd event
callback = <function> Callback function that receives a MessageEnd event

handleStreamEnd(callback)
Handles a StreamEnd event
callback = <function> Callback function that receives a StreamEnd event

handleMessage([sizeLimit, ]callback)
Handles a complete message including the head and the body
sizeLimit = <number|string> Maximum number of bytes to collect from the message body
callback = <function> Callback function that receives a complete message

handleStreamStart(callback)
Handles the initial event in a pipeline
callback = <function> Callback function that receives the initial event

replaceMessageBody([sizeLimit, ]replacement)
Replaces an entire message body
sizeLimit = <number|string> Maximum number of bytes to collect from the message body
callback = <object|function> Replacement events or a callback function that returns replacement events

replaceData([replacement])
Replaces a Data event
replacement = <object|function> Replacement events or a callback function that returns replacement events

replaceMessageStart([replacement])
Replaces a MessageStart event
replacement = <object|function> Replacement events or a callback function that returns replacement events

replaceMessageEnd([replacement])
Replaces a MessageEnd event
replacement = <object|function> Replacement events or a callback function that returns replacement events

replaceStreamEnd([replacement])
Replaces a StreamEnd event
replacement = <object|function> Replacement events or a callback function that returns replacement events

replaceMessage([sizeLimit, ]replacement)
Replaces a complete message including the head and the body
sizeLimit = <number|string> Maximum number of bytes to collect from the message body
callback = <object|function> Replacement events or a callback function that returns replacement events

replaceStreamStart(callback)
Replaces the initial event in a pipeline
callback = <object|function> Replacement events or a callback function that returns replacement events

)***" };
