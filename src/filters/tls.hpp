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

namespace tls {

class TLSFilter;

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

class TLSSession :
  public pjs::Pooled<TLSSession>,
  public EventProxy
{
public:
  static void init();
  static auto get(SSL *ssl) -> TLSSession*;

  TLSSession(
    TLSContext *ctx,
    Pipeline *pipeline,
    bool is_server,
    pjs::Object *certificate
  );

  ~TLSSession();

  void set_sni(const char *name);
  void start_handshake();

private:
  SSL* m_ssl;
  BIO* m_rbio;
  BIO* m_wbio;
  Data m_buffer_write;
  Data m_buffer_receive;
  pjs::Ref<Pipeline> m_pipeline;
  pjs::Ref<pjs::Object> m_certificate;
  pjs::Ref<pjs::Object> m_ca;
  bool m_is_server;
  bool m_closed = false;

  virtual void on_input(Event *evt) override;
  virtual void on_reply(Event *evt) override;

  void on_receive_peer(Event *evt);
  void on_server_name();

  void use_certificate(pjs::Str *sni);
  bool do_handshake();
  auto pump_send(bool flush = false) -> int;
  auto pump_receive() -> int;
  void pump_read();
  void pump_write();
  void close(StreamEnd::Error err = StreamEnd::NO_ERROR);

  static int s_user_data_index;

  friend class pjs::RefCount<TLSSession>;
  friend class TLSContext;
};

//
// Client
//

class Client : public Filter {
public:
  Client(pjs::Object *options);

private:
  Client(const Client &r);
  ~Client();

  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void process(Event *evt) override;
  virtual void dump(std::ostream &out) override;

  std::shared_ptr<TLSContext> m_tls_context;
  pjs::Ref<pjs::Object> m_certificate;
  TLSSession* m_session = nullptr;
  pjs::Value m_sni;
};

//
// Server
//

class Server : public Filter {
public:
  Server(pjs::Object *options);

private:
  Server(const Server &r);
  ~Server();

  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void process(Event *evt) override;
  virtual void dump(std::ostream &out) override;

  std::shared_ptr<TLSContext> m_tls_context;
  pjs::Ref<pjs::Object> m_certificate;
  TLSSession* m_session = nullptr;
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
  virtual void dump(std::ostream &out) override;

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
