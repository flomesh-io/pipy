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

#ifndef TLS_HPP
#define TLS_HPP

#include "filter.hpp"
#include "data.hpp"
#include "api/crypto.hpp"
#include "options.hpp"

#include <openssl/bio.h>
#include <openssl/ssl.h>

#include <vector>
#include <string>
#include <set>

namespace pipy {

namespace tls {

class TLSFilter;

//
// ProtocolVersion
//

enum class ProtocolVersion {
  TLS1,
  TLS1_1,
  TLS1_2,
  TLS1_3,
};

//
// Options
//

struct Options : public pipy::Options {
  pjs::EnumValue<ProtocolVersion> minVersion = ProtocolVersion::TLS1_2;
  pjs::EnumValue<ProtocolVersion> maxVersion = ProtocolVersion::TLS1_3;
  pjs::Ref<pjs::Str> ciphers;
  pjs::Ref<pjs::Object> certificate;
  std::vector<pjs::Ref<crypto::Certificate>> trusted;
  pjs::Ref<pjs::Function> handshake;
  pjs::Ref<pjs::Function> on_verify_f;
  pjs::Ref<pjs::Function> on_state_f;
  bool alpn = false;
#if PIPY_USE_NTLS
  bool ntls = false;
#endif

  Options() {}
  Options(pjs::Object *options, const char *base_name = nullptr);
};

//
// TLSContext
//

class TLSContext {
public:
  TLSContext(bool is_server, const Options &options);
  ~TLSContext();

  auto ctx() const -> SSL_CTX* { return m_ctx; }
  void set_protocol_versions(ProtocolVersion min, ProtocolVersion max);
  void set_ciphers(const std::string &ciphers);
  void set_dhparam(const std::string &data);
  void add_certificate(crypto::Certificate *cert);
  void set_client_alpn(const std::vector<std::string> &protocols);
  void set_server_alpn(const std::set<pjs::Ref<pjs::Str>> &protocols);

private:
  SSL_CTX* m_ctx;
  DH* m_dhparam = nullptr;
  X509_STORE* m_verify_store;
  std::set<pjs::Ref<pjs::Str>> m_server_alpn;

  static auto on_verify(int preverify_ok, X509_STORE_CTX *ctx) -> int;
  static auto on_server_name(SSL *ssl, int*, void*) -> int;
  static auto on_select_alpn(
    SSL *ssl,
    const unsigned char **out,
    unsigned char *outlen,
    const unsigned char *in,
    unsigned int inlen,
    void *arg
  ) -> int;
};

//
// TLSSession
//

class TLSSession :
  public pjs::ObjectTemplate<TLSSession>,
  public EventProxy
{
public:
  //
  // TLSSession::State
  //

  enum class State {
    idle,
    handshake,
    connected,
    closed,
  };

  //
  // TLSSession::HandshakeInfo
  // TODO: remove this
  //

  struct HandshakeInfo : public pjs::ObjectTemplate<HandshakeInfo> {
    pjs::Ref<pjs::Str> alpn;
  };

  static void init();
  static auto get(SSL *ssl) -> TLSSession*;

  void start_handshake(const char *name = nullptr);

  auto state() const -> State { return m_state; }
  auto error() const -> pjs::Str* { return m_error; }
  auto protocol() -> pjs::Str*;

private:
  TLSSession(
    TLSContext *ctx,
    Filter *filter,
    bool is_server,
#if PIPY_USE_NTLS
    bool is_ntls,
#endif
    pjs::Object *certificate,
    pjs::Function *alpn,
    pjs::Function *handshake,
    pjs::Function *on_verify,
    pjs::Function *on_state
  );

  ~TLSSession();

  Filter* m_filter;
  SSL* m_ssl;
  BIO* m_rbio;
  BIO* m_wbio;
  Data m_buffer_write;
  Data m_buffer_receive;
  State m_state = State::idle;
  pjs::Ref<Pipeline> m_pipeline;
  pjs::Ref<pjs::Object> m_certificate;
  pjs::Ref<pjs::Object> m_ca;
  pjs::Ref<pjs::Function> m_alpn;
  pjs::Ref<pjs::Function> m_handshake;
  pjs::Ref<pjs::Function> m_on_verify;
  pjs::Ref<pjs::Function> m_on_state;
  pjs::Ref<pjs::Str> m_error;
  pjs::Ref<pjs::Str> m_protocol;
  bool m_is_server;
#if PIPY_USE_NTLS
  bool m_is_ntls;
#endif
  bool m_closed_input = false;
  bool m_closed_output = false;

  virtual void on_input(Event *evt) override;
  virtual void on_reply(Event *evt) override;

  void on_receive_peer(Event *evt);
  auto on_verify(int preverify_ok, X509_STORE_CTX *ctx) -> int;
  void on_server_name();
  auto on_select_alpn(pjs::Array *names) -> int;

  void set_state(State state);
  void use_certificate(pjs::Str *sni);
  bool handshake_step();
  void handshake_done();
  auto pump_send() -> int;
  auto pump_receive() -> int;
  void pump_read();
  void pump_write();
  void close();
  void error();

  static int s_user_data_index;

  friend class pjs::ObjectTemplate<TLSSession>;
  friend class TLSContext;
};

//
// Client
//

class Client : public Filter {
public:
  struct Options : public tls::Options {
    std::vector<std::string> alpn_list;
    pjs::Ref<pjs::Str> sni;
    pjs::Ref<pjs::Function> sni_f;

    Options() {}
    Options(pjs::Object *options, const char *base_name = nullptr);
  };

  Client(const Options &options);

private:
  Client(const Client &r);
  ~Client();

  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void process(Event *evt) override;
  virtual void dump(Dump &d) override;

  std::shared_ptr<TLSContext> m_tls_context;
  std::shared_ptr<Options> m_options;
  pjs::Ref<TLSSession> m_session;
};

//
// Server
//

class Server : public Filter {
public:
  struct Options : public tls::Options {
    pjs::Ref<pjs::Str> dhparam_s;
    pjs::Ref<Data> dhparam;
    pjs::Ref<pjs::Function> alpn_f;
    std::set<pjs::Ref<pjs::Str>> alpn_set;

    Options() {}
    Options(pjs::Object *options);
  };

  Server(const Options &options);

private:
  Server(const Server &r);
  ~Server();

  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void process(Event *evt) override;
  virtual void dump(Dump &d) override;

  std::shared_ptr<TLSContext> m_tls_context;
  std::shared_ptr<Options> m_options;
  pjs::Ref<TLSSession> m_session;
};

//
// OnClientHello
//

class OnClientHello : public Filter {
public:
  OnClientHello(pjs::Function *callback);

private:
  OnClientHello(const OnClientHello &r);
  ~OnClientHello();

  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void process(Event *evt) override;
  virtual void dump(Dump &d) override;

  enum State {
    READ_TYPE,
    READ_SIZE,
    READ_DATA,
    DONE,
  };

  pjs::Ref<pjs::Function> m_callback;
  State m_rec_state = READ_TYPE;
  State m_hsk_state = READ_TYPE;
  uint8_t m_rec_read_size;
  uint8_t m_hsk_read_size;
  uint16_t m_rec_data_size;
  uint32_t m_hsk_data_size;
  Data m_message;
};

} // namespace tls
} // namespace pipy

#endif // TLS_HPP
