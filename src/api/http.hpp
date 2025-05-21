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

#ifndef API_HTTP_HPP
#define API_HTTP_HPP

#include "pjs/pjs.hpp"
#include "pipeline.hpp"
#include "message.hpp"
#include "tar.hpp"
#include "options.hpp"

#include <string>
#include <vector>
#include <unordered_map>

namespace pipy {

class Tarball;

namespace http {

enum class TunnelType {
  NONE,
  CONNECT,
  WEBSOCKET,
  HTTP2,
};

class MessageHead : public pjs::ObjectTemplate<MessageHead> {
public:
  pjs::Ref<pjs::Str> protocol;
  pjs::Ref<pjs::Object> headers;
  pjs::Ref<pjs::Object> headerNames;
  bool isChunked = false;

  bool is_final() const;
  bool is_final(pjs::Str *header_connection) const;
};

class MessageTail : public pjs::ObjectTemplate<MessageTail> {
public:
  pjs::Ref<pjs::Object> headers;
  int headSize = 0;
  int bodySize = 0;
};

class RequestHead : public pjs::ObjectTemplate<RequestHead, MessageHead> {
public:
  pjs::Ref<pjs::Str> method;
  pjs::Ref<pjs::Str> scheme;
  pjs::Ref<pjs::Str> authority;
  pjs::Ref<pjs::Str> path;

  auto tunnel_type() const -> TunnelType;
  auto tunnel_type(pjs::Str *header_upgrade) const -> TunnelType;
};

class ResponseHead : public pjs::ObjectTemplate<ResponseHead, MessageHead> {
public:
  int status = 200;
  pjs::Ref<pjs::Str> statusText;

  bool is_tunnel_ok(TunnelType requested);

  static auto error_to_status(StreamEnd::Error err, int &status) -> pjs::Str*;
};

//
// Match
//

class Match : public pjs::FunctionTemplate<Match> {
public:
  auto exec(const std::string &path) -> pjs::Object*;
  auto exec(pjs::Object *head) -> pjs::Object*;

  void operator()(pjs::Context &ctx, pjs::Object *obj, pjs::Value &ret);

private:
  Match(const std::string &path);

  struct Section {
    pjs::Ref<pjs::Str> name;
    std::string match;
  };

  std::vector<Section> m_sections;

  friend class pjs::ObjectTemplate<Match, pjs::Function>;
};

//
// Agent
//

class Agent : public pjs::ObjectTemplate<Agent> {
public:
  auto request(Message *req) -> pjs::Promise*;
  auto request(pjs::Str *method, pjs::Str *path, pjs::Object *headers = nullptr, Data *body = nullptr) -> pjs::Promise*;
  auto request(pjs::Str *method, pjs::Str *path, pjs::Object *headers, pjs::Str *body) -> pjs::Promise*;

private:
  Agent(pjs::Str *host, pjs::Object *options = nullptr);

  //
  // Agent::Request
  //

  class Request : public pjs::Pooled<Request>, public EventSource {
  public:
    Request(Agent *agent) : m_agent(agent) {}

    auto start(RequestHead *head, Data *body) -> pjs::Promise*;

  private:
    void send(RequestHead *head, Data *body);

    pjs::Ref<Agent> m_agent;
    pjs::Ref<Pipeline> m_pipeline;
    pjs::Ref<pjs::Promise::Settler> m_settler;
    MessageReader m_message_reader;

    virtual void on_reply(Event *evt) override;
  };

  pjs::Ref<PipelineLayout> m_pipeline_layout;
  pjs::Ref<pjs::Str> m_host;

  static Data::Producer s_dp;

  friend class pjs::ObjectTemplate<Agent>;
};

//
// Directory
//

class Directory : public pjs::ObjectTemplate<Directory> {
public:
  struct Options : public pipy::Options {
    bool fs = false;
    bool tarball = false;
    pjs::Ref<pjs::Str> index;
    pjs::Ref<pjs::Array> index_list;
    pjs::Ref<pjs::Object> content_types;
    pjs::Ref<pjs::Function> content_types_f;
    pjs::Ref<pjs::Str> default_content_type;
    pjs::Ref<pjs::Function> compression_f;
    Options() {}
    Options(pjs::Object *options);
  };

  Directory(const std::string &path);
  Directory(const std::string &path, const Options &options);
  ~Directory();

  auto serve(pjs::Context &ctx, Message *request) -> Message*;
  void set_content_types(pjs::Object *obj);

private:
  struct File {
    pjs::Ref<pjs::Str> pathname;
    pjs::Ref<pjs::Str> content_type;
    Data raw, gz, br;
  };

  class Loader {
  public:
    virtual ~Loader() {}
    virtual bool load_file(const std::string &path, Data &data) = 0;
  };

  class CodebaseLoader : public Loader {
  public:
    CodebaseLoader(const std::string &path);
    virtual bool load_file(const std::string &path, Data &data) override;
    std::string m_root_path;
  };

  class FileSystemLoader : public Loader {
  public:
    FileSystemLoader(const std::string &path);
    virtual bool load_file(const std::string &path, Data &data) override;
    std::string m_root_path;
  };

  class TarballLoader : public Loader {
  public:
    TarballLoader(const char *data, size_t size);
    virtual bool load_file(const std::string &path, Data &data) override;
    Tarball m_tarball;
  };

  Options m_options;
  Loader* m_loader = nullptr;
  std::unordered_map<std::string, File> m_cache;
  std::list<std::string> m_index_filenames;
  std::map<std::string, pjs::Ref<pjs::Str>> m_content_types;
  pjs::Ref<pjs::Str> m_default_content_type;

  auto get_encoded_response(pjs::Context &ctx, File &file, RequestHead *request) -> Message*;

  static Data::Producer s_dp;
};

//
// File
//

class File : public pjs::ObjectTemplate<File> {
public:
  static auto from(const std::string &path) -> File*;
  static auto from(Tarball *tarball, const std::string &path) -> File*;

  auto to_message(pjs::Str *accept_encoding) -> Message*;

private:
  File(const std::string &path);
  File(Tarball *tarball, const std::string &path);

  pjs::Ref<pjs::Str> m_path;
  pjs::Ref<pjs::Str> m_name;
  pjs::Ref<pjs::Str> m_extension;
  pjs::Ref<pjs::Str> m_content_type;
  pjs::Ref<Data> m_data;
  pjs::Ref<Data> m_data_gz;
  pjs::Ref<Data> m_data_br;
  pjs::Ref<Message> m_message;
  pjs::Ref<Message> m_message_gz;
  pjs::Ref<Message> m_message_br;

  void load(const std::string &filename, std::function<Data*(const std::string&)> get_file);
  bool decompress();

  friend class pjs::ObjectTemplate<File>;
};

//
// Http
//

class Http : public pjs::ObjectTemplate<Http>
{
};

} // namespace http
} // namespace pipy

#endif // API_HTTP_HPP
