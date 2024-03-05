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

#ifndef CRYPTO_HPP
#define CRYPTO_HPP

#include "pjs/pjs.hpp"
#include "data.hpp"

#include <openssl/evp.h>

namespace pipy {
namespace crypto {

//
// PublicKey
//

class PublicKey : public pjs::ObjectTemplate<PublicKey> {
public:
  auto pkey() const -> EVP_PKEY* { return m_pkey; }

private:
  PublicKey(Data *data);
  PublicKey(pjs::Str *data);
  ~PublicKey();

  EVP_PKEY* m_pkey = nullptr;

  static auto read_pem(const void *data, size_t size) -> EVP_PKEY*;
  static auto load_by_engine(const std::string &id) -> EVP_PKEY*;

  friend class pjs::ObjectTemplate<PublicKey>;
};

//
// PrivateKey
//

class PrivateKey : public pjs::ObjectTemplate<PrivateKey> {
public:
  auto pkey() const -> EVP_PKEY* { return m_pkey; }

private:
  PrivateKey(Data *data);
  PrivateKey(pjs::Str *data);
  ~PrivateKey();

  EVP_PKEY* m_pkey = nullptr;

  static auto read_pem(const void *data, size_t size) -> EVP_PKEY*;
  static auto load_by_engine(const std::string &id) -> EVP_PKEY*;

  friend class pjs::ObjectTemplate<PrivateKey>;
};

//
// Certificate
//

class Certificate : public pjs::ObjectTemplate<Certificate> {
public:
  auto x509() const -> X509* { return m_x509; }

  auto issuer() -> pjs::Object*;
  auto subject() -> pjs::Object*;
  auto subject_alt_names() -> pjs::Array*;

private:
  Certificate(X509 *x509);
  Certificate(Data *data);
  Certificate(pjs::Str *data);
  ~Certificate();

  X509* m_x509 = nullptr;
  pjs::Ref<pjs::Object> m_issuer;
  pjs::Ref<pjs::Object> m_subject;
  pjs::Ref<pjs::Array> m_subject_alt_names;

  static auto read_pem(const void *data, size_t size) -> X509*;
  static auto get_x509_name(X509_NAME *name) -> pjs::Object*;

  friend class pjs::ObjectTemplate<Certificate>;
};

//
// CertificateChain
//

class CertificateChain : public pjs::ObjectTemplate<CertificateChain> {
public:
  auto size() const -> int { return m_x509s.size(); }
  auto x509(int i) const -> X509* { return m_x509s[i]; }

private:
  CertificateChain(Data *data);
  CertificateChain(pjs::Str *data);
  ~CertificateChain();

  std::vector<X509*> m_x509s;

  void load_chain(const char *str);

  static auto read_pem(const void *data, size_t size) -> X509*;

  friend class pjs::ObjectTemplate<CertificateChain>;
};

//
// Cipher
//

class Cipher : public pjs::ObjectTemplate<Cipher> {
public:
  static auto cipher(const std::string &algorithm) -> const EVP_CIPHER*;

  auto update(Data *data) -> Data*;
  auto update(pjs::Str *str) -> Data*;
  auto final() -> Data*;

private:
  Cipher(const std::string &algorithm, pjs::Object *options);
  ~Cipher();

  EVP_CIPHER_CTX* m_ctx = nullptr;

  friend class pjs::ObjectTemplate<Cipher>;
};

//
// Decipher
//

class Decipher : public pjs::ObjectTemplate<Decipher> {
public:
  auto update(Data *data) -> Data*;
  auto update(pjs::Str *str) -> Data*;
  auto final() -> Data*;

private:
  Decipher(const std::string &algorithm, pjs::Object *options);
  ~Decipher();

  EVP_CIPHER_CTX* m_ctx = nullptr;

  friend class pjs::ObjectTemplate<Decipher>;
};

//
// Hash
//

class Hash : public pjs::ObjectTemplate<Hash> {
public:
  static auto digest(const std::string &algorithm) -> const EVP_MD*;

  void update(Data *data);
  void update(pjs::Str *str, Data::Encoding enc = Data::Encoding::utf8);
  void update(const std::string &str, Data::Encoding enc = Data::Encoding::utf8);
  auto digest() -> Data*;
  auto digest(void *hash) -> size_t;
  auto digest(Data::Encoding enc) -> pjs::Str*;

private:
  Hash(const std::string &algorithm);
  ~Hash();

  EVP_MD_CTX* m_ctx = nullptr;

  friend class pjs::ObjectTemplate<Hash>;
};

//
// Hmac
//

class Hmac : public pjs::ObjectTemplate<Hmac> {
public:
  void update(Data *data);
  void update(pjs::Str *str, Data::Encoding enc);
  auto digest() -> Data*;
  auto digest(Data::Encoding enc) -> pjs::Str*;

private:
  Hmac(const std::string &algorithm, Data *key);
  Hmac(const std::string &algorithm, pjs::Str *key);
  ~Hmac();

  HMAC_CTX* m_ctx = nullptr;

  friend class pjs::ObjectTemplate<Hmac>;
};

//
// Sign
//

class Sign : public pjs::ObjectTemplate<Sign> {
public:
  void update(Data *data);
  void update(pjs::Str *str, Data::Encoding enc);
  auto sign(PrivateKey *key, Object *options = nullptr) -> Data*;
  auto sign(PrivateKey *key, Data::Encoding enc, Object *options = nullptr) -> pjs::Str*;

private:
  Sign(const std::string &algorithm);
  ~Sign();

  const EVP_MD* m_md = nullptr;
  EVP_MD_CTX* m_ctx = nullptr;

  friend class pjs::ObjectTemplate<Sign>;
};

//
// Verify
//

class Verify : public pjs::ObjectTemplate<Verify> {
public:
  void update(Data *data);
  void update(pjs::Str *str, Data::Encoding enc);
  bool verify(PublicKey *key, Data *signature, Object *options = nullptr);
  bool verify(PublicKey *key, pjs::Str *signature, Data::Encoding enc, Object *options = nullptr);

private:
  Verify(const std::string &algorithm);
  ~Verify();

  const EVP_MD* m_md = nullptr;
  EVP_MD_CTX* m_ctx = nullptr;

  friend class pjs::ObjectTemplate<Verify>;
};

//
// JWK
//

class JWK : public pjs::ObjectTemplate<JWK> {
public:
  bool is_valid() const { return m_pkey; }
  auto pkey() const -> EVP_PKEY* { return m_pkey; }

private:
  JWK(pjs::Object *json);
  ~JWK();

  EVP_PKEY* m_pkey = nullptr;

  friend class pjs::ObjectTemplate<JWK>;
};

//
// JWT
//

class JWT : public pjs::ObjectTemplate<JWT> {
public:
  enum class Algorithm {
    HS256,
    HS384,
    HS512,
    RS256,
    RS384,
    RS512,
    ES256,
    ES384,
    ES512,
  };

  bool is_valid() const { return m_is_valid; }
  auto header() const -> const pjs::Value& { return m_header; }
  auto payload() const -> const pjs::Value& { return m_payload; }
  void sign(pjs::Str *key);
  bool verify(Data *key);
  bool verify(pjs::Str *key);
  bool verify(JWK *key);
  bool verify(PublicKey *key);

private:
  JWT(pjs::Str *token);
  ~JWT();

  bool m_is_valid = false;
  Algorithm m_algorithm = Algorithm::HS256;
  pjs::Value m_header;
  pjs::Value m_payload;
  std::string m_header_str;
  std::string m_payload_str;
  std::string m_signature_str;
  std::string m_signature;

  auto get_md() -> const EVP_MD*;
  bool verify(const char *key, int key_len);
  bool verify(EVP_PKEY *pkey);
  int jose2der(char *out, const char *inp, int len);

  friend class pjs::ObjectTemplate<JWT>;
};

//
// Crypto
//

class Crypto : public pjs::ObjectTemplate<Crypto> {
public:
  static auto get_openssl_engine() -> ENGINE*;
  static void init(const std::string &engine_id);
  static void free();
};

} // namespace crypto
} // namespace pipy

#endif // CRYPTO_HPP
