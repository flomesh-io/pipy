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
// Cipher
//

class Cipher : public pjs::ObjectTemplate<Cipher> {
public:
  enum class Algorithm {
    sm4_cbc,
    sm4_ecb,
    sm4_cfb,
    sm4_cfb128,
    sm4_ofb,
    sm4_ctr,
  };

  auto update(Data *data) -> Data*;
  auto update(pjs::Str *str) -> Data*;
  auto final() -> Data*;

private:
  Cipher(Algorithm algorithm, pjs::Object *options);
  ~Cipher();

  EVP_CIPHER_CTX* m_cctx = nullptr;

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
  Decipher(Cipher::Algorithm algorithm, pjs::Object *options);
  ~Decipher();

  EVP_CIPHER_CTX* m_cctx = nullptr;

  friend class pjs::ObjectTemplate<Decipher>;
};

//
// Hash
//

class Hash : public pjs::ObjectTemplate<Hash> {
public:
  enum class Algorithm {
    md4,
    md5,
    md5_sha1,
    blake2b512,
    blake2s256,
    sha1,
    sha224,
    sha256,
    sha384,
    sha512,
    sha512_224,
    sha512_256,
    sha3_224,
    sha3_256,
    sha3_384,
    sha3_512,
    shake128,
    shake256,
    mdc2,
    ripemd160,
    whirlpool,
    sm3,
  };

  static auto digest(Algorithm algorithm) -> const EVP_MD*;

  void update(Data *data);
  void update(pjs::Str *str, Data::Encoding enc);
  auto digest() -> Data*;
  auto digest(Data::Encoding enc) -> pjs::Str*;

private:
  Hash(Algorithm algorithm);
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
  Hmac(Hash::Algorithm algorithm, Data *key);
  Hmac(Hash::Algorithm algorithm, pjs::Str *key);
  ~Hmac();

  HMAC_CTX* m_ctx = nullptr;

  friend class pjs::ObjectTemplate<Hmac>;
};

//
// Verify
//

class Verify : public pjs::ObjectTemplate<Verify> {
public:
  void update(Data *data);
  void update(pjs::Str *str, Data::Encoding enc);
  bool verify(pjs::Str *key, Data *signature);
  bool verify(pjs::Str *key, pjs::Str *signature, Data::Encoding enc);

private:
  Verify(Hash::Algorithm algorithm);
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

class Crypto : public pjs::ObjectTemplate<Crypto>
{
};

} // namespace crypto
} // namespace pipy

#endif // CRYPTO_HPP