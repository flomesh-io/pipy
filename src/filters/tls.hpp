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

#include <openssl/bio.h>
#include <openssl/ssl.h>

namespace pipy {

class Session;

namespace tls {

//
// TLSContext
//

class TLSContext {
public:
  TLSContext();
  ~TLSContext();

  auto ctx() const -> SSL_CTX* { return m_ctx; }
  void add_certificate(crypto::Certificate *cert);

private:
  SSL_CTX* m_ctx;
  X509_STORE* m_verify_store;

  static auto on_server_name(SSL *ssl, int*, void*) -> int;
};

//
// TLSSession
//

class TLSSession : public pjs::Pooled<TLSSession, pjs::RefCount<TLSSession>> {
public:
  static void init();
  static auto get(SSL *ssl) -> TLSSession*;

  TLSSession(
    TLSContext *ctx,
    bool is_server,
    pjs::Object *certificate,
    Session *session,
    const Event::Receiver &output
  );

  ~TLSSession();

  void set_sni(const char *name);
  void start_handshake();
  void input(Data *data);

  void on_server_name();

private:
  SSL* m_ssl;
  BIO* m_rbio;
  BIO* m_wbio;
  Data m_buffer_send;
  Data m_buffer_receive;
  pjs::Ref<Session> m_session;
  pjs::Ref<pjs::Object> m_certificate;
  pjs::Ref<pjs::Object> m_ca;
  Event::Receiver m_output;
  bool m_is_server;
  bool m_closed = false;

  void use_certificate(pjs::Str *sni);
  bool do_handshake();
  auto pump_send() -> int;
  auto pump_receive() -> int;
  void pump_write();
  void pump_read();
  void close(SessionEnd *end = nullptr);

  static int s_user_data_index;
};

//
// Client
//

class Client : public Filter {
public:
  Client();
  Client(pjs::Str *target, pjs::Object *options);

private:
  Client(const Client &r);
  ~Client();

  virtual auto help() -> std::list<std::string> override;
  virtual void dump(std::ostream &out) override;
  virtual auto draw(std::list<std::string> &links, bool &fork) -> std::string override;
  virtual void bind() override;
  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void process(Context *ctx, Event *inp) override;

private:
  Pipeline* m_pipeline = nullptr;
  pjs::Ref<pjs::Str> m_target;
  pjs::Ref<pjs::Object> m_certificate;
  pjs::Ref<TLSSession> m_session;
  pjs::Value m_sni;
  std::shared_ptr<TLSContext> m_tls_context;
  bool m_session_end = false;
};

//
// Server
//

class Server : public Filter {
public:
  Server();
  Server(pjs::Str *target, pjs::Object *options);

private:
  Server(const Server &r);
  ~Server();

  virtual auto help() -> std::list<std::string> override;
  virtual void dump(std::ostream &out) override;
  virtual auto draw(std::list<std::string> &links, bool &fork) -> std::string override;
  virtual void bind() override;
  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void process(Context *ctx, Event *inp) override;

private:
  Pipeline* m_pipeline = nullptr;
  pjs::Ref<pjs::Str> m_target;
  pjs::Ref<pjs::Object> m_certificate;
  pjs::Ref<TLSSession> m_session;
  std::shared_ptr<TLSContext> m_tls_context;
  bool m_session_end = false;
};

} // namespace tls
} // namespace pipy

#endif // TLS_HPP