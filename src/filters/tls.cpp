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
#include "pipeline.hpp"
#include "api/crypto.hpp"
#include "log.hpp"

#include <openssl/err.h>
#include <openssl/opensslv.h>
#ifdef PIPY_USE_PQC
#ifdef PIPY_USE_OQS_PROVIDER
#include <openssl/provider.h>
#endif
#endif

namespace pipy {
namespace tls {

thread_local static pjs::ConstStr STR_serverNames("serverNames");
thread_local static pjs::ConstStr STR_protocolNames("protocolNames");
static Data::Producer s_dp("TLS");

static void throw_error() {
  char str[1000];
  auto err = ERR_get_error();
  ERR_error_string(err, str);
  throw std::runtime_error(str);
}

//
// Options
//

//
// PqcOptions
//

#ifdef PIPY_USE_PQC
PqcOptions::PqcOptions(pjs::Object *options) {
  Value(options, "keyExchange")
    .get(key_exchange)
    .check_nullable();

  Value(options, "signature")
    .get(signature)
    .check_nullable();
}
#endif

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

  Value(options, "handshake", base_name)
    .get(handshake)
    .check_nullable();

  Value(options, "verify", base_name)
    .get(on_verify_f)
    .check_nullable();

  Value(options, "onVerify", base_name)
    .get(on_verify_f)
    .check_nullable();

  Value(options, "onState", base_name)
    .get(on_state_f)
    .check_nullable();

#if PIPY_USE_NTLS
  Value(options, "ntls", base_name)
    .get(ntls)
    .check_nullable();
#endif

#ifdef PIPY_USE_PQC
  pjs::Ref<pjs::Object> pqc_options;
  Value(options, "pqc", base_name)
    .get(pqc_options)
    .check_nullable();

  if (pqc_options) {
    pqc = PqcOptions(pqc_options);

    // No PQC signature support available
    if (pqc.signature && !TLSContext::openssl_supports_pqc_signatures()) {
      Log::warn("[tls] PQC signature algorithms are not available in this build configuration, ignoring signature setting");
      pqc.signature = nullptr;
    }
  }
#endif
}

//
// TLSContext
//

TLSContext::TLSContext(bool is_server, const Options &options) {
#if PIPY_USE_NTLS
  if(options.ntls) {
    m_ctx = SSL_CTX_new(is_server ? NTLS_server_method() : NTLS_client_method());
  } else {
    m_ctx = SSL_CTX_new(is_server ? TLS_server_method() : TLS_client_method());
  }
#else
  m_ctx = SSL_CTX_new(is_server ? TLS_server_method() : TLS_client_method());
#endif

  if (!m_ctx) throw_error();
#if PIPY_USE_NTLS
  if (options.ntls) {
    SSL_CTX_enable_ntls(m_ctx);
  }
#endif
  m_verify_store = X509_STORE_new();
  if (!m_verify_store) throw_error();

  SSL_CTX_set0_verify_cert_store(m_ctx, m_verify_store);
  SSL_CTX_set_tlsext_servername_callback(m_ctx, on_server_name);

  if (options.alpn && is_server) {
    SSL_CTX_set_alpn_select_cb(m_ctx, on_select_alpn, this);
  }

#ifdef PIPY_USE_PQC
  if (options.pqc.key_exchange || options.pqc.signature) {
    // Load oqs-provider if needed and available
    if (should_use_oqs_provider()) {
#ifdef PIPY_USE_OQS_PROVIDER
      load_pqc_provider();
#else
      Log::warn("[tls] PQC support requires oqs-provider but it was not built into this binary");
#endif
    }
    
    // Both key_exchange and signature have been processed with defaults/nulls in Options constructor
    std::string sig_alg = options.pqc.signature ? options.pqc.signature->str() : "";
    set_pqc_algorithms(
      options.pqc.key_exchange->str(),
      sig_alg
    );
  }
#endif
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
  SSL_CTX_set_ciphersuites(m_ctx, ciphers.c_str());
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
    return SSL_TLSEXT_ERR_ALERT_FATAL;
  }
}

#ifdef PIPY_USE_PQC
#ifdef PIPY_USE_OQS_PROVIDER
void TLSContext::load_pqc_provider() {
  if (!OSSL_PROVIDER_load(nullptr, "oqsprovider")) {
    throw std::runtime_error("Failed to load OQS provider");
  }
}
#endif

bool TLSContext::openssl_supports_pqc_signatures() {
  // OpenSSL 3.2.0 introduced PQC signature support via oqs-provider
  // OpenSSL 3.5.0+ has built-in PQC signature support
  // 3.2.0 = 0x30200000L
  return OPENSSL_VERSION_NUMBER >= 0x30200000L;
}

bool TLSContext::should_use_oqs_provider() {
#ifdef PIPY_PQC_BUILTIN_ONLY
  // Force built-in only mode
  return false;
#elif defined(PIPY_USE_OQS_PROVIDER)
  // OpenSSL 3.5.0 has built-in PQC algorithms
  // Version format: MNNFFPPS (Major, miNor, Fix, Patch, Status)
  // 3.5.0 = 0x30500000L
  // Use oqs-provider for OpenSSL versions that don't have built-in PQC
  // or when explicitly configured to use oqs-provider
  return OPENSSL_VERSION_NUMBER < 0x30500000L;
#else
  // No oqs-provider available
  return false;
#endif
}

void TLSContext::set_pqc_algorithms(const std::string &kem_alg, const std::string &sig_alg) {
  // Configure supported groups (KEM algorithms)
  if (!kem_alg.empty()) {
    // CRITICAL: OpenSSL 3.5.2 uses different names at different layers
    // 
    // Reference: deps/openssl-3.5.2/doc/designs/ML-KEM.md lines 15-18:
    // "The key management and KEM algorithm names are ML-KEM-512, ML-KEM-768
    //  and ML-KEM-1024. At the TLS layer, the associated key exchange groups are,
    //  respectively, MLKEM512, MLKEM768 and MLKEM1024."
    //
    // Implementation evidence: deps/openssl-3.5.2/providers/common/capabilities.c lines 179-181:
    //   TLS_GROUP_ENTRY("MLKEM512", "", "ML-KEM-512", 38),
    //   TLS_GROUP_ENTRY("MLKEM768", "", "ML-KEM-768", 39), 
    //   TLS_GROUP_ENTRY("MLKEM1024", "", "ML-KEM-1024", 40),
    //
    // This name mapping is essential: SSL_CTX_set1_groups_list() expects TLS group names
    // (lowercase: mlkem512, mlkem768, mlkem1024), not algorithm names (ML-KEM-*).
    std::string openssl_kem_name = kem_alg;
    if (kem_alg == "ML-KEM-512") {
      openssl_kem_name = "mlkem512";
    } else if (kem_alg == "ML-KEM-768") {
      openssl_kem_name = "mlkem768";
    } else if (kem_alg == "ML-KEM-1024") {
      openssl_kem_name = "mlkem1024";
    }
    // Hybrid algorithms already use correct TLS group names:
    // X25519MLKEM768, SecP256r1MLKEM768, SecP384r1MLKEM1024, X448MLKEM1024
    
    if (SSL_CTX_set1_groups_list(m_ctx, openssl_kem_name.c_str()) != 1) {
      throw std::runtime_error("Failed to set PQC KEM algorithms: " + openssl_kem_name);
    }
  }

  // Configure signature algorithms
  if (!sig_alg.empty()) {
    // CRITICAL: SLH-DSA has special API requirements in OpenSSL 3.5.2
    // 
    // Reference: deps/openssl-3.5.2/doc/designs/slh-dsa.md lines 96-114:
    // "As only the one-shot implementation is required and the message is not digested
    //  the APIs used should be EVP_PKEY_sign_message_init(), EVP_PKEY_sign(),
    //  EVP_PKEY_verify_message_init(), EVP_PKEY_verify().
    //  
    //  For backwards compatibility reasons EVP_DigestSignInit_ex(), EVP_DigestSign(),
    //  EVP_DigestVerifyInit_ex() and EVP_DigestVerify() may also be used, but the digest
    //  passed in mdname must be NULL (i.e. it effectively behaves the same as above)."
    //
    // Root cause: SSL_CTX_set1_sigalgs_list() expects traditional digest-based signature 
    // algorithms, but SLH-DSA is designed as a "one-shot" algorithm that doesn't use 
    // separate digest operations.
    //
    // Solution: Let the TLS layer automatically derive SLH-DSA algorithms from certificates.
    // This approach:
    // 1. Avoids the API mismatch between TLS sigalgs and SLH-DSA's one-shot design
    // 2. Uses OpenSSL's intended certificate-based algorithm detection
    // 3. Allows all SLH-DSA variants (SHA2/SHAKE, 128s/128f/192s/256s) to work correctly
    bool is_slh_dsa = (sig_alg.find("SLH-DSA") != std::string::npos);
    
    if (is_slh_dsa) {
      // Skip explicit signature algorithm setting for SLH-DSA variants
      // The TLS layer will automatically use the correct algorithm from the certificate
    } else {
      // Handle ML-DSA and other traditional signature algorithms normally
      // ML-DSA algorithms (ML-DSA-44, ML-DSA-65, ML-DSA-87) work with SSL_CTX_set1_sigalgs_list()
      // because they follow the traditional digest-based signature API model
      if (SSL_CTX_set1_sigalgs_list(m_ctx, sig_alg.c_str()) != 1) {
        throw std::runtime_error("Failed to set PQC signature algorithms: " + sig_alg);
      }
    }
  }
}
#endif

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
#if PIPY_USE_NTLS
  bool is_ntls,
#endif
  pjs::Object *certificate,
  pjs::Function *alpn,
  pjs::Function *handshake,
  pjs::Function *on_verify,
  pjs::Function *on_state
)
  : m_filter(filter)
  , m_certificate(certificate)
  , m_alpn(alpn)
  , m_handshake(handshake)
  , m_on_verify(on_verify)
  , m_on_state(on_state)
  , m_is_server(is_server)
#if PIPY_USE_NTLS
  , m_is_ntls(is_ntls)
#endif
{
  m_ssl = SSL_new(ctx->ctx());
  SSL_set_ex_data(m_ssl, s_user_data_index, this);

  m_rbio = BIO_new(BIO_s_mem());
  m_wbio = BIO_new(BIO_s_mem());

  SSL_set_bio(m_ssl, m_rbio, m_wbio);

  m_pipeline = filter->sub_pipeline(0, false, reply())->start();
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
  SSL_free(m_ssl);
}

void TLSSession::start_handshake(const char *name) {
  if (name) SSL_set_tlsext_host_name(m_ssl, name);
  handshake_step();
}

auto TLSSession::protocol() -> pjs::Str* {
  if (!m_protocol) {
    const unsigned char *str = nullptr;
    unsigned int len = 0;
    SSL_get0_alpn_selected(m_ssl, &str, &len);
    if (str) {
      m_protocol = pjs::Str::make((const char *)str, len);
    }
  }
  return m_protocol;
}

auto TLSSession::hostname() -> pjs::Str* {
  if (!m_hostname) {
    if (auto name = SSL_get_servername(m_ssl, TLSEXT_NAMETYPE_host_name)) {
      m_hostname = pjs::Str::make(name);
    }
  }
  return m_hostname;
}

auto TLSSession::peer() -> crypto::Certificate* {
  if (!m_peer) {
#ifndef PIPY_USE_OPENSSL1
    if (auto x = SSL_get0_peer_certificate(m_ssl)) {
      m_peer = crypto::Certificate::make(x);
    }
#else
    if (auto x = SSL_get_peer_certificate(m_ssl)) {
      m_peer = crypto::Certificate::make(x);
      X509_free(x);
    }
#endif
  }
  return m_peer;
}

void TLSSession::on_input(Event *evt) {
  if (m_closed_input) return;

  if (Data::is_flush(evt)) {
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

  if (Data::is_flush(evt)) {
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
  if (!m_on_verify) return preverify_ok;
  auto *x509 = X509_STORE_CTX_get0_cert(cert_store_ctx);
  pjs::Ref<crypto::Certificate> cert(crypto::Certificate::make(x509));
  pjs::Value args[2], ret;
  args[0].set((bool)preverify_ok);
  args[1].set(cert.get());
  Context &ctx = *m_pipeline->context();
  (*m_on_verify)(ctx, 2, args, ret);
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
    return -1;
  }
}

void TLSSession::set_state(State state) {
  m_state = state;
  if (m_on_state) {
    Context &ctx = *m_pipeline->context();
    pjs::Value arg(this), ret;
    (*m_on_state)(ctx, 1, &arg, ret);
    ctx.reset(); // TODO: print out errors if any
  }
}

void TLSSession::set_error() {
  Data buf;
  Data::Builder db(buf, &s_dp);
  while (auto err = ERR_get_error()) {
    char str[256];
    ERR_error_string(err, str);
    Log::warn("[tls] %s", str);
    if (db.size() > 0) db.push('\n');
    db.push(str, std::strlen(str));
  }
  db.flush();
  m_error = pjs::Str::make(buf.to_string());
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

  if (certificate.is_nullish()) return;

  if (!certificate.is_object()) {
    m_filter->error("certificate callback did not return an object");
    return;
  }

  pjs::Value cert, key;
  certificate.o()->get("cert", cert);
  certificate.o()->get("key", key);

  if (!key.is<crypto::PrivateKey>()) {
    m_filter->error("certificate.key requires a PrivateKey object");
    return;
  }

#if PIPY_USE_NTLS
  if (m_is_ntls) {
    pjs::Value cert_enc, cert_sign, key_enc, key_sign;
    certificate.o()->get("certSign", cert_sign);
    certificate.o()->get("certEnc", cert_enc);
    certificate.o()->get("keySign", key_sign);
    certificate.o()->get("keyEnc", key_enc);

    if (!key_sign.is_nullish() && !key_sign.is<crypto::PrivateKey>()) {
      m_filter->error("certificate.keySign requires a PrivateKey object");
      return;
    }

    if (!key_enc.is_nullish() && !key_enc.is<crypto::PrivateKey>()) {
      m_filter->error("certificate.keyEnc requires a PrivateKey object");
      return;
    }

    if (!cert_sign.is_nullish() && !cert_sign.is<crypto::Certificate>()) {
      m_filter->error("certificate.certSign requires a Certificate object");
      return;
    }

    if (!cert_enc.is_nullish() && !cert_enc.is<crypto::Certificate>()) {
      m_filter->error("certificate.certEnc requires a Certificate object");
      return;
    }

    if (!key_sign.is_nullish()) {
      if (SSL_use_sign_PrivateKey(m_ssl,key_sign.as<crypto::PrivateKey>()->pkey()) == 0) throw_error();
    }

    if (!key_enc.is_nullish()) {
      if (SSL_use_enc_PrivateKey(m_ssl,key_enc.as<crypto::PrivateKey>()->pkey()) == 0) throw_error();
    }

    if (!cert_sign.is_nullish()) {
      if (SSL_use_sign_certificate(m_ssl,cert_sign.as<crypto::Certificate>()->x509()) == 0) throw_error();
    }

    if (!cert_enc.is_nullish()) {
      if (SSL_use_enc_certificate(m_ssl,cert_enc.as<crypto::Certificate>()->x509()) == 0) throw_error();
    }
  }
#endif

  SSL_use_PrivateKey(m_ssl, key.as<crypto::PrivateKey>()->pkey());

  if (cert.is<crypto::Certificate>()) {
    SSL_use_certificate(m_ssl, cert.as<crypto::Certificate>()->x509());
  } else if (cert.is<crypto::CertificateChain>()) {
    auto chain = cert.as<crypto::CertificateChain>();
    if (chain->size() < 1) {
      m_filter->error("empty certificate chain");
    } else {
      SSL_use_certificate(m_ssl, chain->x509(0));
      for (int i = 1; i < chain->size(); i++) {
        SSL_add1_chain_cert(m_ssl, chain->x509(i));
      }
    }
  } else {
    m_filter->error("certificate.cert requires a Certificate or a CertificateChain object");
  }
}

bool TLSSession::handshake_step() {
  if (m_state == State::idle) set_state(State::handshake);
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
      Log::warn("[tls] handshake failed (error = %d)", status);
      set_error();
      close();
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
    auto info = HandshakeInfo::make();
    const unsigned char *str = nullptr;
    unsigned int len = 0;
    SSL_get0_alpn_selected(m_ssl, &str, &len);
    info->alpn = pjs::Str::make((const char *)str, len);
    pjs::Value arg(info), ret;
    (*m_handshake)(ctx, 1, &arg, ret);
  }
  set_state(State::connected);
  if (m_is_server) {
    forward(Data::make());
  } else {
    output(Data::make());
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
          close();
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
          close();
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

void TLSSession::close() {
  if (m_is_server) {
    if (!m_closed_output) {
      m_closed_output = true;
      output(StreamEnd::make());
    }
  } else {
    if (!m_closed_input) {
      m_closed_input = true;
      forward(StreamEnd::make());
    }
  }
  set_state(State::closed);
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

  alpn = alpn_string || alpn_array;

  if (alpn_string) {
    alpn_list.push_back(alpn_string->str());
  } else if (alpn_array) {
    alpn_list.resize(alpn_array->length());
    alpn_array->iterate_all(
      [&](pjs::Value &v, int i) {
        if (!v.is_string()) {
          char msg[100];
          auto options = base_name ? base_name : "options";
          std::sprintf(msg, "%s.alpn[%d] expects a string", options, i);
          throw std::runtime_error(msg);
        }
        alpn_list[i] = v.s()->str();
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
  : m_tls_context(std::make_shared<TLSContext>(false, options))
  , m_options(std::make_shared<Options>(options))
{
#if PIPY_USE_NTLS
  if (!options.ntls) {
    m_tls_context->set_protocol_versions(
      options.minVersion,
      options.maxVersion
    );
  }
#else
  m_tls_context->set_protocol_versions(
    options.minVersion,
    options.maxVersion
  );
#endif

  if (options.ciphers) {
    m_tls_context->set_ciphers(options.ciphers->str());
  }

  for (const auto &cert : options.trusted) {
    m_tls_context->add_certificate(cert);
  }

  if (options.alpn_list.size() > 0) {
    m_tls_context->set_client_alpn(options.alpn_list);
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
    m_session = TLSSession::make(
      m_tls_context.get(),
      this,
      false,
#if PIPY_USE_NTLS
      m_options->ntls,
#endif
      m_options->certificate,
      nullptr,
      m_options->handshake,
      m_options->on_verify_f,
      m_options->on_state_f
    );
    m_session->chain(Filter::output());
    pjs::Value sni(m_options->sni);
    if (!eval(m_options->sni_f, sni)) return;
    if (sni.is_nullish()) {
      m_session->start_handshake();
    } else {
      auto s = sni.to_string();
      m_session->start_handshake(s->c_str());
      s->release();
    }
  }

  m_session->input()->input(evt);
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
    .get(alpn_f)
    .get(alpn_array)
    .check_nullable();

  alpn = alpn_f || alpn_array;

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
  : m_tls_context(std::make_shared<TLSContext>(true, options))
  , m_options(std::make_shared<Options>(options))
{
#if PIPY_USE_NTLS
  if (!options.ntls) {
    m_tls_context->set_protocol_versions(
      options.minVersion,
      options.maxVersion
    );
  }
#else
  m_tls_context->set_protocol_versions(
    options.minVersion,
    options.maxVersion
  );
#endif

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
  m_session = nullptr;
}

void Server::process(Event *evt) {
  if (!m_session) {
    m_session = TLSSession::make(
      m_tls_context.get(),
      this,
      true,
#if PIPY_USE_NTLS
      m_options->ntls,
#endif
      m_options->certificate,
      m_options->alpn_f,
      m_options->handshake,
      m_options->on_verify_f,
      m_options->on_state_f
    );
    m_session->chain(Filter::output());
  }

  m_session->input()->input(evt);
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
            pjs::vl_array<uint8_t> buf(size);
            if (!read(buf, size)) return false;
            names->push(pjs::Str::make((const char *)buf.data(), size));
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
            pjs::vl_array<uint8_t> buf(len);
            if (!read(buf, len)) return false;
            names->push(pjs::Str::make((const char *)buf.data(), len));
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

template<> void EnumDef<TLSSession::State>::init() {
  define(TLSSession::State::idle, "idle");
  define(TLSSession::State::handshake, "handshake");
  define(TLSSession::State::connected, "connected");
  define(TLSSession::State::closed, "closed");
}

template<> void ClassDef<TLSSession::HandshakeInfo>::init() {
  field<Ref<Str>>("alpn", [](TLSSession::HandshakeInfo *obj) { return &obj->alpn; });
}

template<> void ClassDef<TLSSession>::init() {
  accessor("state", [](Object *obj, Value &ret) { ret.set(EnumDef<TLSSession::State>::name(obj->as<TLSSession>()->state())); });
  accessor("error", [](Object *obj, Value &ret) { ret.set(obj->as<TLSSession>()->error()); });
  accessor("protocol", [](Object *obj, Value &ret) { ret.set(obj->as<TLSSession>()->protocol()); });
  accessor("hostname", [](Object *obj, Value &ret) { ret.set(obj->as<TLSSession>()->hostname()); });
  accessor("peer", [](Object *obj, Value &ret) { ret.set(obj->as<TLSSession>()->peer()); });
}

} // namespace pjs
