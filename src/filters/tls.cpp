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

#include "tls.hpp"
#include "context.hpp"
#include "module.hpp"
#include "pipeline.hpp"
#include "api/crypto.hpp"
#include "log.hpp"

#include <openssl/err.h>

#if PIPY_USE_RFC8998
#include "rfc-8998.hpp"
#endif

namespace pipy {
namespace tls {

thread_local static pjs::ConstStr STR_serverNames("serverNames");
thread_local static pjs::ConstStr STR_protocolNames("protocolNames");
thread_local static Data::Producer s_dp("TLS");

static void throw_error() {
  char str[1000];
  auto err = ERR_get_error();
  ERR_error_string(err, str);
  throw std::runtime_error(str);
}

//
// TLSContext
//

TLSContext::TLSContext(bool is_server) {
#if PIPY_USE_RFC8998
  m_ctx = SSL_CTX_new(is_server ? TLS_server_method() : TLS_client_method_rfc8998());
#else
  m_ctx = SSL_CTX_new(is_server ? TLS_server_method() : TLS_client_method());
#endif

  if (!m_ctx) throw_error();

  m_verify_store = X509_STORE_new();
  if (!m_verify_store) throw_error();

  SSL_CTX_set0_verify_cert_store(m_ctx, m_verify_store);
  SSL_CTX_set_tlsext_servername_callback(m_ctx, on_server_name);

  if (is_server) {
    SSL_CTX_set_alpn_select_cb(m_ctx, on_select_alpn, this);
  }
}

TLSContext::~TLSContext() {
  if (m_dhparam) DH_free(m_dhparam);
  if (m_ctx) SSL_CTX_free(m_ctx);
}

void TLSContext::set_protocol_versions(ProtocolVersion min, ProtocolVersion max) {
  auto f = [](ProtocolVersion v) {
    switch (v) {
      case ProtocolVersion::TLS1  : return TLS1_VERSION;
      case ProtocolVersion::TLS1_1: return TLS1_1_VERSION;
      case ProtocolVersion::TLS1_2: return TLS1_2_VERSION;
      case ProtocolVersion::TLS1_3: return TLS1_3_VERSION;
    }
    return SSL3_VERSION;
  };
  SSL_CTX_set_min_proto_version(m_ctx, f(min));
  SSL_CTX_set_max_proto_version(m_ctx, f(max));
}

void TLSContext::set_ciphers(const std::string &ciphers) {
  SSL_CTX_set_cipher_list(m_ctx, ciphers.c_str());
}

void TLSContext::set_dhparam(const std::string &data) {
  if (data == "auto") {
    SSL_CTX_set_dh_auto(m_ctx, 1);
  } else {
    auto bio = BIO_new_mem_buf(data.c_str(), data.length());
    m_dhparam = PEM_read_bio_DHparams(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    SSL_CTX_set_dh_auto(m_ctx, 0);
    if (m_dhparam) SSL_CTX_set_tmp_dh(m_ctx, m_dhparam);
  }
}

void TLSContext::add_certificate(crypto::Certificate *cert) {
  X509_STORE_add_cert(m_verify_store, cert->x509());
  SSL_CTX_set_verify(m_ctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, on_verify);
}

void TLSContext::set_client_alpn(const std::vector<std::string> &protocols) {
  std::string proto_list;
  for (const auto &s : protocols) {
    if (s.length() == 0 || s.length() > 255) {
      std::string msg("protocol name is empty or too long for ALPN: ");
      throw std::runtime_error(msg + s);
    }
    proto_list += char(s.length());
    proto_list += s;
  }
  SSL_CTX_set_alpn_protos(m_ctx, (const unsigned char *)proto_list.c_str(), proto_list.size());
}

void TLSContext::set_server_alpn(const std::set<pjs::Ref<pjs::Str>> &protocols) {
  m_server_alpn = protocols;
}

auto TLSContext::on_verify(int preverify_ok, X509_STORE_CTX *ctx) -> int {
  auto *ssl = (SSL*)X509_STORE_CTX_get_ex_data(ctx, SSL_get_ex_data_X509_STORE_CTX_idx());
  return TLSSession::get(ssl)->on_verify(preverify_ok, ctx);
}

auto TLSContext::on_server_name(SSL *ssl, int*, void *thiz) -> int {
  TLSSession::get(ssl)->on_server_name();
  return SSL_TLSEXT_ERR_OK;
}

auto TLSContext::on_select_alpn(
  SSL *ssl,
  const unsigned char **out,
  unsigned char *outlen,
  const unsigned char *in,
  unsigned int inlen,
  void *arg
) -> int {
  const unsigned char *names[100];
  unsigned int p = 0, n = 0;
  while (p < inlen && n < 100) {
    auto len = in[p];
    names[n++] = in + p;
    p += len + 1;
  }
  pjs::Ref<pjs::Array> name_array = pjs::Array::make(n);
  for (size_t i = 0; i < n; i++) {
    auto str = (const char *)names[i] + 1;
    auto len = *names[i];
    auto s = pjs::Str::make(str, len);
    name_array->set(i, s);
    if (static_cast<TLSContext*>(arg)->m_server_alpn.count(s)) {
      *out = (const unsigned char *)str;
      *outlen = len;
      return SSL_TLSEXT_ERR_OK;
    }
  }
  auto sel = TLSSession::get(ssl)->on_select_alpn(name_array);
  if (0 <= sel && sel < n) {
    *out = names[sel] + 1;
    *outlen = *names[sel];
    return SSL_TLSEXT_ERR_OK;
  } else {
    return SSL_TLSEXT_ERR_NOACK;
  }
}

//
// TLSSession
//
// Server-side:
//                +------+-----+
// --- receive -->| rbio |     |--- read -->
//                |------| SSL |
// <-- send ------| wbio |     |<-- write --
//                +------+-----+
//
// Client-side:
//                +-----+------+
// --- write ---->|     | wbio |--- send ----->
//                | SSL |------|
// <-- read ------|     | rbio |<-- receive ---
//                +-----+------+
//

int TLSSession::s_user_data_index = 0;

void TLSSession::init() {
  SSL_load_error_strings();
  SSL_library_init();

  s_user_data_index = SSL_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr);
}

auto TLSSession::get(SSL *ssl) -> TLSSession* {
  auto ptr = SSL_get_ex_data(ssl, s_user_data_index);
  return reinterpret_cast<TLSSession*>(ptr);
}

TLSSession::TLSSession(
  TLSContext *ctx,
  Filter *filter,
  bool is_server,
  pjs::Object *certificate,
  pjs::Function *verify,
  pjs::Function *alpn,
  pjs::Function *handshake
)
  : m_certificate(certificate)
  , m_verify(verify)
  , m_alpn(alpn)
  , m_handshake(handshake)
  , m_is_server(is_server)
{
  m_ssl = SSL_new(ctx->ctx());
  SSL_set_ex_data(m_ssl, s_user_data_index, this);

  m_rbio = BIO_new(BIO_s_mem());
  m_wbio = BIO_new(BIO_s_mem());

  SSL_set_bio(m_ssl, m_rbio, m_wbio);

  m_pipeline = filter->sub_pipeline(0, false, reply());
  chain_forward(m_pipeline->input());

  if (is_server) {
    SSL_set_accept_state(m_ssl);
    use_certificate(nullptr);

  } else {
    SSL_set_connect_state(m_ssl);
    if (m_certificate) use_certificate(nullptr);
  }
}

TLSSession::~TLSSession() {
  close();
  SSL_free(m_ssl);
}

void TLSSession::set_sni(const char *name) {
  SSL_set_tlsext_host_name(m_ssl, name);
}

void TLSSession::start_handshake() {
  handshake_step();
}

void TLSSession::on_input(Event *evt) {
  if (m_closed_input) return;

  if (evt->is<MessageEnd>() || Data::is_flush(evt)) {
    forward(evt);

  } else if (auto *data = evt->as<Data>()) {
    if (m_is_server) {
      m_buffer_receive.push(*data);
      if (handshake_step()) pump_read();
    } else {
      m_buffer_write.push(*data);
      if (handshake_step()) pump_write();
    }

  } else if (evt->is<StreamEnd>()) {
    m_closed_input = true;
    forward(evt);
  }
}

void TLSSession::on_reply(Event *evt) {
  if (m_closed_output) return;

  if (evt->is<MessageEnd>() || Data::is_flush(evt)) {
    output(evt);

  } else if (auto *data = evt->as<Data>()) {
    if (m_is_server) {
      m_buffer_write.push(*data);
      if (handshake_step()) pump_write();
    } else {
      m_buffer_receive.push(*data);
      if (handshake_step()) pump_read();
    }

  } else if (evt->is<StreamEnd>()) {
    m_closed_output = true;
    output(evt);
  }
}

auto TLSSession::on_verify(int preverify_ok, X509_STORE_CTX *cert_store_ctx) -> int {
  if (!m_verify) return preverify_ok;
  auto *x509 = X509_STORE_CTX_get0_cert(cert_store_ctx);
  pjs::Ref<crypto::Certificate> cert(crypto::Certificate::make(x509));
  pjs::Value args[2], ret;
  args[0].set((bool)preverify_ok);
  args[1].set(cert.get());
  Context &ctx = *m_pipeline->context();
  (*m_verify)(ctx, 2, args, ret);
  if (!ctx.ok()) return 0;
  return ret.to_boolean();
}

void TLSSession::on_server_name() {
  if (auto name = SSL_get_servername(m_ssl, TLSEXT_NAMETYPE_host_name)) {
    pjs::Ref<pjs::Str> sni(pjs::Str::make(name));
    use_certificate(sni);
  }
}

auto TLSSession::on_select_alpn(pjs::Array *names) -> int {
  if (m_alpn) {
    Context &ctx = *m_pipeline->context();
    pjs::Value arg(names), ret;
    (*m_alpn)(ctx, 1, &arg, ret);
    if (!ctx.ok()) return -1;
    return ret.to_number();
  } else {
    return 0;
  }
}

void TLSSession::use_certificate(pjs::Str *sni) {
  pjs::Value certificate(m_certificate);
  if (certificate.is_function()) {
    Context &ctx = *m_pipeline->context();
    pjs::Value arg;
    if (sni) arg.set(sni);
    (*certificate.f())(ctx, 1, &arg, certificate);
    if (!ctx.ok()) return;
  }

  if (!certificate.is_object()) {
    Log::error("[tls] certificate callback did not return an object");
    return;
  }

  if (certificate.is_null()) return;

  pjs::Value cert, key;
  certificate.o()->get("cert", cert);
  certificate.o()->get("key", key);

  if (!key.is<crypto::PrivateKey>()) {
    Log::error("[tls] certificate.key requires a PrivateKey object");
    return;
  }

  SSL_use_PrivateKey(m_ssl, key.as<crypto::PrivateKey>()->pkey());

  if (cert.is<crypto::Certificate>()) {
    SSL_use_certificate(m_ssl, cert.as<crypto::Certificate>()->x509());
  } else if (cert.is<crypto::CertificateChain>()) {
    auto chain = cert.as<crypto::CertificateChain>();
    if (chain->size() < 1) {
      Log::error("[tls] empty certificate chain");
    } else {
      SSL_use_certificate(m_ssl, chain->x509(0));
      for (int i = 1; i < chain->size(); i++) {
        SSL_add1_chain_cert(m_ssl, chain->x509(i));
      }
    }
  } else {
    Log::error("[tls] certificate.cert requires a Certificate or a CertificateChain object");
  }

#if PIPY_USE_RFC8998
  pjs::Value cert_enc, key_enc;
  certificate.o()->get("certEnc", cert_enc);
  certificate.o()->get("keyEnc", key_enc);

  if (!key_enc.is_undefined() && !key_enc.is<crypto::PrivateKey>()) {
    Log::error("[tls] certificate.keyEnc requires a PrivateKey object");
    return;
  }

  if (!cert_enc.is_undefined() && !cert_enc.is<crypto::Certificate>()) {
    Log::error("[tls] certificate.certEnc requires a Certificate object");
    return;
  }

  if (!key_enc.is_undefined()) {
    SSL_use_PrivateKey_rfc8998(m_ssl, key_enc.as<crypto::PrivateKey>()->pkey());
  }

  if (!cert_enc.is_undefined()) {
    SSL_use_certificate_rfc8998(m_ssl, cert_enc.as<crypto::Certificate>()->x509());
  }
#endif
}

bool TLSSession::handshake_step() {
  while (!SSL_is_init_finished(m_ssl)) {
    pump_receive();
    int ret = SSL_do_handshake(m_ssl);
    if (ret == 1) {
      handshake_done();
      pump_send();
      pump_write();
      return true;
    }
    if (!ret) {
      close();
      return false;
    }
    bool blocked = false;
    auto status = SSL_get_error(m_ssl, ret);
    if (status == SSL_ERROR_WANT_READ) {
      if (m_buffer_receive.empty()) {
        blocked = true;
      }
    } else if (status != SSL_ERROR_WANT_WRITE) {
      Log::warn("[tls] Handshake failed (error = %d)", status);
      while (auto err = ERR_get_error()) {
        char str[256];
        ERR_error_string(err, str);
        Log::warn("[tls] %s", str);
      }
      close(StreamEnd::UNAUTHORIZED);
      return false;
    }
    pump_send();
    if (blocked) return false;
  }
  return true;
}

void TLSSession::handshake_done() {
  if (m_handshake) {
    Context &ctx = *m_pipeline->context();
    pjs::Value arg, ret;
    const unsigned char *str = nullptr;
    unsigned int len = 0;
    SSL_get0_alpn_selected(m_ssl, &str, &len);
    if (str) arg.set(pjs::Str::make((const char *)str, len));
    (*m_handshake)(ctx, 1, &arg, ret);
    if (m_is_server) {
      forward(Data::make());
    } else {
      output(Data::make());
    }
  }
}

auto TLSSession::pump_send() -> int {
  int size = 0;
  for (;;) {
    size_t n = 0;
    Data data(DATA_CHUNK_SIZE, &s_dp);
    auto chunk = data.chunks().begin();
    auto ptr = std::get<0>(*chunk);
    auto len = std::get<1>(*chunk);
    if (BIO_read_ex(m_wbio, ptr, len, &n)) {
      data.pop(data.size() - n);
      if (m_is_server) {
        output(Data::make(data));
      } else {
        forward(Data::make(data));
      }
      size += n;
    } else {
      break;
    }
  }
  return size;
}

auto TLSSession::pump_receive() -> int {
  int size = 0;
  for (const auto c : m_buffer_receive.chunks()) {
    auto ptr = std::get<0>(c);
    auto len = std::get<1>(c);
    size_t n = 0;
    if (!BIO_write_ex(m_rbio, ptr, len, &n)) break;
    size += n;
    if (n < len) break;
  }
  m_buffer_receive.shift(size);
  return size;
}

void TLSSession::pump_read() {
  for (;;) {
    for (;;) {
      size_t n = 0;
      Data data(DATA_CHUNK_SIZE, &s_dp);
      auto chunk = data.chunks().begin();
      auto buf = std::get<0>(*chunk);
      auto len = std::get<1>(*chunk);
      auto ret = SSL_read_ex(m_ssl, buf, len, &n);
      if (ret <= 0) {
        int status = SSL_get_error(m_ssl, ret);
        if (status == SSL_ERROR_ZERO_RETURN) {
          close();
          return;
        } else if (status == SSL_ERROR_WANT_READ || status == SSL_ERROR_WANT_WRITE) {
          break;
        } else {
          close(StreamEnd::READ_ERROR);
          return;
        }
      } else {
        data.pop(data.size() - n);
        if (m_is_server) {
          forward(Data::make(data));
        } else {
          output(Data::make(data));
        }
      }
    }
    if (pump_send() + pump_receive() == 0) break;
  }
}

void TLSSession::pump_write() {
  while (!m_buffer_write.empty()) {
    int size = 0;
    for (const auto c : m_buffer_write.chunks()) {
      size_t n = 0;
      auto ptr = std::get<0>(c);
      auto len = std::get<1>(c);
      auto ret = SSL_write_ex(m_ssl, ptr, len, &n);
      if (ret < 0) {
        int status = SSL_get_error(m_ssl, ret);
        if (status == SSL_ERROR_ZERO_RETURN) {
          close();
          return;
        } else if (status == SSL_ERROR_WANT_READ || status == SSL_ERROR_WANT_WRITE) {
          break;
        } else {
          close(StreamEnd::WRITE_ERROR);
          return;
        }
      }
      size += n;
      if (n < len) break;
    }
    m_buffer_write.shift(size);
    if (pump_send() + pump_receive() == 0) break;
  }
}

void TLSSession::close(StreamEnd::Error err) {
  if (m_is_server) {
    if (!m_closed_output) {
      m_closed_output = true;
      output(StreamEnd::make(err));
    }
  } else {
    if (m_closed_input) {
      m_closed_input = true;
      forward(StreamEnd::make(err));
    }
  }
}

//
// Options
//

Options::Options(pjs::Object *options, const char *base_name) {
  Value(options, "minVersion")
    .get(minVersion)
    .check_nullable();

  Value(options, "maxVersion")
    .get(maxVersion)
    .check_nullable();

  Value(options, "ciphers")
    .get(ciphers)
    .check_nullable();

  Value(options, "certificate", base_name)
    .get(certificate)
    .check_nullable();

  pjs::Ref<pjs::Array> trusted_array;
  Value(options, "trusted", base_name)
    .get(trusted_array)
    .check_nullable();

  if (trusted_array) {
    trusted.resize(trusted_array->length());
    trusted_array->iterate_all([&](pjs::Value &v, int i) {
      if (!v.is<crypto::Certificate>()) {
        char msg[100];
        auto options = base_name ? base_name : "options";
        std::sprintf(msg, "%s.trusted[%d] expects an object of type crypto.Certificate", options, i);
        throw std::runtime_error(msg);
      }
      trusted[i] = v.as<crypto::Certificate>();
    });
  }

  Value(options, "verify", base_name)
    .get(verify)
    .check_nullable();

  Value(options, "handshake", base_name)
    .get(handshake)
    .check_nullable();
}

//
// Client::Options
//

Client::Options::Options(pjs::Object *options, const char *base_name)
  : tls::Options(options, base_name)
{
  pjs::Ref<pjs::Str> alpn_string;
  pjs::Ref<pjs::Array> alpn_array;
  Value(options, "alpn", base_name)
    .get(alpn_string)
    .get(alpn_array)
    .check_nullable();

  if (alpn_string) {
    alpn.push_back(alpn_string->str());
  } else if (alpn_array) {
    alpn.resize(alpn_array->length());
    alpn_array->iterate_all(
      [&](pjs::Value &v, int i) {
        if (!v.is_string()) {
          char msg[100];
          auto options = base_name ? base_name : "options";
          std::sprintf(msg, "%s.alpn[%d] expects a string", options, i);
          throw std::runtime_error(msg);
        }
        alpn[i] = v.s()->str();
      }
    );
  }

  Value(options, "sni", base_name)
    .get(sni)
    .get(sni_f)
    .check_nullable();
}

//
// Client
//

Client::Client(const Options &options)
  : m_tls_context(std::make_shared<TLSContext>(false))
  , m_options(std::make_shared<Options>(options))
{
  m_tls_context->set_protocol_versions(
    options.minVersion,
    options.maxVersion
  );

  if (options.ciphers) {
    m_tls_context->set_ciphers(options.ciphers->str());
  }

  for (const auto &cert : options.trusted) {
    m_tls_context->add_certificate(cert);
  }

  if (options.alpn.size() > 0) {
    m_tls_context->set_client_alpn(options.alpn);
  }
}

Client::Client(const Client &r)
  : Filter(r)
  , m_tls_context(r.m_tls_context)
  , m_options(r.m_options)
{
}

Client::~Client() {
}

void Client::dump(Dump &d) {
  Filter::dump(d);
  d.name = "connectTLS";
}

auto Client::clone() -> Filter* {
  return new Client(*this);
}

void Client::reset() {
  Filter::reset();
  delete m_session;
  m_session = nullptr;
}

void Client::process(Event *evt) {
  if (evt->is<StreamEnd>()) {
    if (m_session) {
      output(evt, m_session->input());
    }
    return;
  }

  if (!m_session) {
    m_session = new TLSSession(
      m_tls_context.get(),
      this,
      false,
      m_options->certificate,
      m_options->verify,
      nullptr,
      m_options->handshake
    );
    m_session->chain(output());
    pjs::Value sni(m_options->sni);
    if (!eval(m_options->sni_f, sni)) return;
    if (!sni.is_undefined()) {
      if (sni.is_string()) {
        m_session->set_sni(sni.s()->c_str());
      } else {
        Log::error("[tls] options.sni did not return a string");
      }
    }
    m_session->start_handshake();
  }

  output(evt, m_session->input());
}

//
// Server::Options
//

Server::Options::Options(pjs::Object *options)
  : tls::Options(options)
{
  Value(options, "dhparam")
    .get(dhparam)
    .get(dhparam_s)
    .check_nullable();

  pjs::Ref<pjs::Array> alpn_array;
  Value(options, "alpn")
    .get(alpn)
    .get(alpn_array)
    .check_nullable();

  if (alpn_array) {
    alpn_array->iterate_all(
      [this](pjs::Value &v, int i) {
        if (!v.is_string()) {
          char msg[100];
          std::sprintf(msg, "options.alpn[%d] expects a string", i);
          throw std::runtime_error(msg);
        }
        alpn_set.insert(v.s());
      }
    );
  }
}

//
// Server
//

Server::Server(const Options &options)
  : m_tls_context(std::make_shared<TLSContext>(true))
  , m_options(std::make_shared<Options>(options))
{
  m_tls_context->set_protocol_versions(
    options.minVersion,
    options.maxVersion
  );

  if (options.ciphers) {
    m_tls_context->set_ciphers(options.ciphers->str());
  }

  if (options.dhparam_s) {
    m_tls_context->set_dhparam(options.dhparam_s->str());
  } else if (options.dhparam) {
    m_tls_context->set_dhparam(options.dhparam->to_string());
  }

  for (const auto &cert : options.trusted) {
    m_tls_context->add_certificate(cert);
  }

  m_tls_context->set_server_alpn(options.alpn_set);
}

Server::Server(const Server &r)
  : Filter(r)
  , m_tls_context(r.m_tls_context)
  , m_options(r.m_options)
{
}

Server::~Server() {
}

void Server::dump(Dump &d) {
  Filter::dump(d);
  d.name = "acceptTLS";
}

auto Server::clone() -> Filter* {
  return new Server(*this);
}

void Server::reset() {
  Filter::reset();
  delete m_session;
  m_session = nullptr;
}

void Server::process(Event *evt) {
  if (!m_session) {
    m_session = new TLSSession(
      m_tls_context.get(),
      this,
      true,
      m_options->certificate,
      m_options->verify,
      m_options->alpn,
      m_options->handshake
    );
    m_session->chain(output());
  }

  output(evt, m_session->input());
}

//
// ClientHelloParser
//

class ClientHelloParser {
public:
  ClientHelloParser(pjs::Object *message, const Data &data)
    : m_message(message)
    , m_reader(data) {}

  bool parse() {
    uint8_t ver_major, ver_minor, random[32];
    if (!read(ver_major)) return false;
    if (!read(ver_minor)) return false;
    if (!read(random, sizeof(random))) return false;

    // legacy session id
    uint8_t len;
    if (!read(len)) return false;
    if (!skip(len)) return false;

    // cipher suites
    uint16_t len2;
    if (!read(len2)) return false;
    if (!skip(len2)) return false;

    // legacy compression methods
    if (!read(len)) return false;
    if (!skip(len)) return false;

    // extensions
    if (!read(len2)) return false;
    for (auto end_all = pos() + len2; pos() < end_all; ) {
      uint16_t type, size;
      if (!read(type)) break;
      if (!read(size)) return false;
      auto end = pos() + size;
      switch (type) {
        case 0: { // server name indication
          pjs::Array *names = pjs::Array::make();
          m_message->set(STR_serverNames, names);
          uint16_t size;
          if (!read(size)) return false;
          for (auto end = pos() + size; pos() < end; ) {
            uint8_t type;
            if (!read(type) || type != 0) return false;
            if (!read(size)) return false;
            uint8_t buf[size];
            if (!read(buf, size)) return false;
            names->push(pjs::Str::make((const char *)buf, size));
          }
          break;
        }
        case 16: { // application-layer protocol negotiation
          pjs::Array *names = pjs::Array::make();
          m_message->set(STR_protocolNames, names);
          uint16_t size;
          if (!read(size)) return false;
          for (auto end = pos() + size; pos() < end; ) {
            uint8_t len;
            if (!read(len)) return false;
            uint8_t buf[len];
            if (!read(buf, len)) return false;
            names->push(pjs::Str::make((const char *)buf, len));
          }
          break;
        }
        default: {
          skip(size);
          break;
        }
      }
      if (pos() > end) return false;
    }

    return true;
  }

private:
  pjs::Object* m_message;
  Data::Reader m_reader;
  int m_position = 0;

  int pos() const {
    return m_position;
  }

  auto read() -> int {
    auto c = m_reader.get();
    if (c >= 0) m_position++;
    return c;
  }

  bool read(uint8_t *buf, int size) {
    for (int i = 0; i < size; i++) {
      if (!read(buf[i])) return false;
    }
    return true;
  }

  bool read(uint8_t &n) {
    int c = read();
    if (c < 0) return false;
    n = c;
    return true;
  }

  bool read(uint16_t &n) {
    auto msb = read(); if (msb < 0) return false;
    auto lsb = read(); if (lsb < 0) return false;
    n = ((msb & 0xff) << 8) | (lsb & 0xff);
    return true;
  }

  bool skip(int size) {
    for (int i = 0; i < size; i++) {
      if (read() < 0) return false;
    }
    return true;
  }
};

//
// OnClientHello
//

OnClientHello::OnClientHello(pjs::Function *callback)
  : m_callback(callback)
{
}

OnClientHello::OnClientHello(const OnClientHello &r)
  : Filter(r)
  , m_callback(r.m_callback)
{
}

OnClientHello::~OnClientHello()
{
}

void OnClientHello::dump(Dump &d) {
  Filter::dump(d);
  d.name = "handleTLSClientHello";
}

auto OnClientHello::clone() -> Filter* {
  return new OnClientHello(*this);
}

void OnClientHello::reset() {
  Filter::reset();
  m_rec_state = READ_TYPE;
  m_hsk_state = READ_TYPE;
  m_message.clear();
}

void OnClientHello::process(Event *evt) {
  if (m_hsk_state != DONE) {
    if (auto data = evt->as<Data>()) {
      Data buf(*data);
      while (!buf.empty()) {
        auto rec_state = m_rec_state;
        auto hsk_state = m_hsk_state;
        pjs::Ref<Data> output(Data::make());

        // byte scan
        buf.shift_to(
          [&](int c) -> bool {
            switch (rec_state) {
              case READ_TYPE:
                if (c != 22) {
                  hsk_state = DONE;
                  return true;
                }
                rec_state = READ_SIZE;
                m_rec_read_size = 4;
                m_rec_data_size = 0;
                break;
              case READ_SIZE:
                m_rec_data_size = uint16_t(m_rec_data_size << 8) | uint8_t(c);
                if (!--m_rec_read_size) {
                  if (!m_rec_data_size) {
                    hsk_state = DONE;
                    return true;
                  }
                  rec_state = READ_DATA;
                  if (hsk_state == READ_DATA) return true;
                }
                break;
              case READ_DATA:
                switch (hsk_state) {
                  case READ_TYPE:
                    if (c != 1) {
                      hsk_state = DONE;
                      return true;
                    }
                    hsk_state = READ_SIZE;
                    m_hsk_read_size = 3;
                    m_hsk_data_size = 0;
                    break;
                  case READ_SIZE:
                    m_hsk_data_size = (m_hsk_data_size << 8) | uint8_t(c);
                    if (!--m_hsk_read_size) {
                      if (!m_hsk_data_size) {
                        hsk_state = DONE;
                        return true;
                      }
                      hsk_state = READ_DATA;
                      return true;
                    }
                    break;
                  case READ_DATA:
                    if (!--m_hsk_data_size) {
                      hsk_state = DONE;
                      return true;
                    }
                    break;
                  default: break;
                }
                if (!--m_rec_data_size) {
                  rec_state = READ_TYPE;
                  if (hsk_state == READ_DATA) return true;
                }
                break;
              default: break;
            }
            return false;
          },
          *output
        );

        // old state
        if (m_hsk_state == READ_DATA) {
          m_message.push(*output);
        }

        // new state
        if (hsk_state == DONE) {
          pjs::Value msg(pjs::Object::make());
          ClientHelloParser parser(msg.o(), m_message);
          if (parser.parse()) {
            pjs::Value ret;
            callback(m_callback, 1, &msg, ret);
          }
          m_message.clear();
          break;
        }

        m_rec_state = rec_state;
        m_hsk_state = hsk_state;
      }
    }
  }

  output(evt);
}

} // namespace tls
} // namespace pipy

namespace pjs {

using namespace pipy::tls;

template<> void EnumDef<ProtocolVersion>::init() {
  define(ProtocolVersion::TLS1, "TLS1");
  define(ProtocolVersion::TLS1_1, "TLS1.1");
  define(ProtocolVersion::TLS1_2, "TLS1.2");
  define(ProtocolVersion::TLS1_3, "TLS1.3");
}

} // namespace pjs
