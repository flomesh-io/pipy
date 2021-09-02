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

#include "crypto.hpp"
#include "utils.hpp"
#include "api/json.hpp"

#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>

#include <stdexcept>

namespace pipy {
namespace crypto {

static Data::Producer s_dp_cipher("Cipher");
static Data::Producer s_dp_decipher("Decipher");
static Data::Producer s_dp_hmac("Hmac");
static Data::Producer s_dp_hash("Hash");
static Data::Producer s_dp_sign("Sign");
static Data::Producer s_dp_verify("Verify");

static void throw_error() {
  char str[1000];
  auto err = ERR_get_error();
  ERR_error_string(err, str);
  throw std::runtime_error(str);
}

//
// Options
//

enum class Options {
  alias_type,
  id,
  key,
  iv,
};

static void set_pkey_options(EVP_PKEY *pkey, pjs::Object *options) {
  if (options) {
    pjs::Value v;
    options->get(pjs::EnumDef<Options>::name(Options::alias_type), v);
    if (!v.is_undefined()) {
      if (!v.is_string()) throw std::runtime_error("options.aliasType requires a string");
      auto alias_type = pjs::EnumDef<AliasType>::value(v.s());
      if (int(alias_type) < 0) throw std::runtime_error("invalid options.aliasType");
      switch (alias_type) {
        case AliasType::sm2:
          if (EVP_PKEY_set_alias_type(pkey, EVP_PKEY_SM2) <= 0) throw_error();
          break;
      }
    }
  }
}

static void set_pkey_ctx_options(EVP_PKEY_CTX *ctx, pjs::Object *options) {
  if (options) {
    pjs::Value v;
    options->get(pjs::EnumDef<Options>::name(Options::id), v);
    if (!v.is_undefined()) {
      if (!v.is<Data>()) throw std::runtime_error("options.id requires a Data object");
      auto buf = v.as<Data>()->to_bytes();
      if (EVP_PKEY_CTX_set1_id(ctx, &buf[0], buf.size()) <= 0) throw_error();
    }
  }
}

static auto get_cipher_key(const EVP_CIPHER *cipher, pjs::Object *options, uint8_t *key) -> size_t {
  pjs::Value val;
  options->get(pjs::EnumDef<Options>::name(Options::key), val);
  if (val.is<Data>()) {
    auto *data = val.as<Data>();
    if (data->size() > EVP_MAX_KEY_LENGTH) throw std::runtime_error("options.key is too long");
    data->to_bytes(key);
    return data->size();
  } else if (val.is_string()) {
    auto *str = val.s();
    if (str->length() > EVP_MAX_KEY_LENGTH) throw std::runtime_error("options.key is too long");
    std::memcpy(key, str->c_str(), str->length());
    return str->length();
  } else {
    throw std::runtime_error("options.key requires a Data object or a string");
  }
}

static auto get_cipher_iv(const EVP_CIPHER *cipher, pjs::Object *options, uint8_t *iv) -> size_t {
  pjs::Value val;
  auto iv_size = EVP_CIPHER_iv_length(cipher);
  options->get(pjs::EnumDef<Options>::name(Options::iv), val);
  if (val.is<Data>()) {
    auto *data = val.as<Data>();
    if (data->size() != iv_size) {
      std::string msg("options.iv length should be ");
      throw std::runtime_error(msg + std::to_string(iv_size));
    }
    data->to_bytes(iv);
    return iv_size;
  } else if (val.is_string()) {
    auto *str = val.s();
    if (str->length() != iv_size) {
      std::string msg("options.iv length should be ");
      throw std::runtime_error(msg + std::to_string(iv_size));
    }
    std::memcpy(iv, str->c_str(), str->length());
    return iv_size;
  } else if (iv_size > 0) {
    throw std::runtime_error("options.iv requires a Data object or a string");
  } else {
    return 0;
  }
}

//
// PublicKey
//

PublicKey::PublicKey(Data *data, pjs::Object *options) {
  if (!data) throw std::runtime_error("data is null");
  auto buf = data->to_bytes();
  m_pkey = read_pem(&buf[0], buf.size());
  set_pkey_options(m_pkey, options);
}

PublicKey::PublicKey(pjs::Str *data, pjs::Object *options) {
  m_pkey = read_pem(data->c_str(), data->length());
  set_pkey_options(m_pkey, options);
}

PublicKey::~PublicKey() {
  if (m_pkey) EVP_PKEY_free(m_pkey);
}

auto PublicKey::read_pem(const void *data, size_t size) -> EVP_PKEY* {
  auto bio = BIO_new_mem_buf(data, size);
  auto pkey = PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr);
  BIO_free(bio);
  if (!pkey) throw_error();
  return pkey;
}

//
// PrivateKey
//

PrivateKey::PrivateKey(Data *data, pjs::Object *options) {
  if (!data) throw std::runtime_error("data is null");
  auto buf = data->to_bytes();
  m_pkey = read_pem(&buf[0], buf.size());
  set_pkey_options(m_pkey, options);
}

PrivateKey::PrivateKey(pjs::Str *data, pjs::Object *options) {
  m_pkey = read_pem(data->c_str(), data->length());
  set_pkey_options(m_pkey, options);
}

PrivateKey::~PrivateKey() {
  if (m_pkey) EVP_PKEY_free(m_pkey);
}

auto PrivateKey::read_pem(const void *data, size_t size) -> EVP_PKEY* {
  auto bio = BIO_new_mem_buf(data, size);
  auto pkey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
  BIO_free(bio);
  if (!pkey) throw_error();
  return pkey;
}

//
// Certificate
//

Certificate::Certificate(Data *data) {
  auto buf = data->to_bytes();
  m_x509 = read_pem(&buf[0], buf.size());
}

Certificate::Certificate(pjs::Str *data) {
  m_x509 = read_pem(data->c_str(), data->length());
}

Certificate::~Certificate() {
  if (m_x509) X509_free(m_x509);
}

auto Certificate::read_pem(const void *data, size_t size) -> X509* {
  auto bio = BIO_new_mem_buf(data, size);
  auto x509 = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
  BIO_free(bio);
  if (!x509) throw_error();
  return x509;
}

//
// Cipher
//

auto Cipher::cipher(Algorithm algorithm) -> const EVP_CIPHER* {
  switch (algorithm) {
    case Algorithm::sm4_cbc    : return EVP_sm4_cbc();
    case Algorithm::sm4_ecb    : return EVP_sm4_ecb();
    case Algorithm::sm4_cfb    : return EVP_sm4_cfb();
    case Algorithm::sm4_cfb128 : return EVP_sm4_cfb128();
    case Algorithm::sm4_ofb    : return EVP_sm4_ofb();
    case Algorithm::sm4_ctr    : return EVP_sm4_ctr();
  }
  return nullptr;
}

Cipher::Cipher(Algorithm algorithm, pjs::Object *options) {
  uint8_t key[EVP_MAX_KEY_LENGTH];
  uint8_t iv[EVP_MAX_IV_LENGTH];

  auto *cipher = Cipher::cipher(algorithm);

  get_cipher_key(cipher, options, key);
  get_cipher_iv(cipher, options, iv);

  m_ctx = EVP_CIPHER_CTX_new();
  if (!m_ctx) throw_error();

  if (!EVP_EncryptInit_ex(m_ctx, cipher, nullptr, key, iv)) throw_error();
}

Cipher::~Cipher() {
  if (m_ctx) EVP_CIPHER_CTX_free(m_ctx);
}

auto Cipher::update(Data *data) -> Data* {
  auto out = Data::make();
  auto block_size = EVP_CIPHER_CTX_block_size(m_ctx);
  uint8_t buf[DATA_CHUNK_SIZE + block_size];
  for (const auto &c : data->chunks()) {
    auto ptr = std::get<0>(c);
    auto len = std::get<1>(c);
    int n = 0;
    if (!EVP_EncryptUpdate(m_ctx, buf, &n, (const unsigned char *)ptr, len)) {
      out->release();
      throw_error();
    }
    s_dp_cipher.push(out, buf, n);
  }
  return out;
}

auto Cipher::update(pjs::Str *str) -> Data* {
  auto out = Data::make();
  auto block_size = EVP_CIPHER_CTX_block_size(m_ctx);
  uint8_t buf[DATA_CHUNK_SIZE + block_size];
  for (size_t i = 0, l = str->length(); i < l; i += DATA_CHUNK_SIZE) {
    int n = 0;
    if (!EVP_EncryptUpdate(
      m_ctx, buf, &n,
      (const unsigned char *)str->c_str() + i,
      std::min(l - i, DATA_CHUNK_SIZE)
    )) {
      out->release();
      throw_error();
    }
    s_dp_cipher.push(out, buf, n);
  }
  return out;
}

auto Cipher::final() -> Data* {
  auto block_size = EVP_CIPHER_CTX_block_size(m_ctx);
  uint8_t buf[block_size];
  int len = 0;
  if (!EVP_EncryptFinal(m_ctx, buf, &len)) throw_error();
  return s_dp_cipher.make((const uint8_t *)buf, len);
}

//
// Decipher
//

Decipher::Decipher(Cipher::Algorithm algorithm, pjs::Object *options) {
  uint8_t key[EVP_MAX_KEY_LENGTH];
  uint8_t iv[EVP_MAX_IV_LENGTH];

  auto *cipher = Cipher::cipher(algorithm);

  get_cipher_key(cipher, options, key);
  get_cipher_iv(cipher, options, iv);

  m_ctx = EVP_CIPHER_CTX_new();
  if (!m_ctx) throw_error();

  if (!EVP_DecryptInit_ex(m_ctx, cipher, nullptr, key, iv)) throw_error();
}

Decipher::~Decipher() {
  if (m_ctx) EVP_CIPHER_CTX_free(m_ctx);
}

auto Decipher::update(Data *data) -> Data* {
  auto out = Data::make();
  auto block_size = EVP_CIPHER_CTX_block_size(m_ctx);
  uint8_t buf[DATA_CHUNK_SIZE + block_size];
  for (const auto &c : data->chunks()) {
    auto ptr = std::get<0>(c);
    auto len = std::get<1>(c);
    int n = 0;
    if (!EVP_DecryptUpdate(m_ctx, buf, &n, (const unsigned char *)ptr, len)) {
      out->release();
      throw_error();
    }
    s_dp_decipher.push(out, buf, n);
  }
  return out;
}

auto Decipher::update(pjs::Str *str) -> Data* {
  auto out = Data::make();
  auto block_size = EVP_CIPHER_CTX_block_size(m_ctx);
  uint8_t buf[DATA_CHUNK_SIZE + block_size];
  for (size_t i = 0, l = str->length(); i < l; i += DATA_CHUNK_SIZE) {
    int n = 0;
    if (!EVP_DecryptUpdate(
      m_ctx, buf, &n,
      (const unsigned char *)str->c_str() + i,
      std::min(l - i, DATA_CHUNK_SIZE)
    )) {
      out->release();
      throw_error();
    }
    s_dp_decipher.push(out, buf, n);
  }
  return out;
}

auto Decipher::final() -> Data* {
  auto block_size = EVP_CIPHER_CTX_block_size(m_ctx);
  uint8_t buf[block_size];
  int len = 0;
  if (!EVP_DecryptFinal(m_ctx, buf, &len)) throw_error();
  return s_dp_decipher.make((const uint8_t *)buf, len);
}

//
// Hash
//

auto Hash::digest(Algorithm algorithm) -> const EVP_MD* {
  switch (algorithm) {
    case Algorithm::md4        : return EVP_md4       ();
    case Algorithm::md5        : return EVP_md5       ();
    case Algorithm::md5_sha1   : return EVP_md5_sha1  ();
    case Algorithm::blake2b512 : return EVP_blake2b512();
    case Algorithm::blake2s256 : return EVP_blake2s256();
    case Algorithm::sha1       : return EVP_sha1      ();
    case Algorithm::sha224     : return EVP_sha224    ();
    case Algorithm::sha256     : return EVP_sha256    ();
    case Algorithm::sha384     : return EVP_sha384    ();
    case Algorithm::sha512     : return EVP_sha512    ();
    case Algorithm::sha512_224 : return EVP_sha512_224();
    case Algorithm::sha512_256 : return EVP_sha512_256();
    case Algorithm::sha3_224   : return EVP_sha3_224  ();
    case Algorithm::sha3_256   : return EVP_sha3_256  ();
    case Algorithm::sha3_384   : return EVP_sha3_384  ();
    case Algorithm::sha3_512   : return EVP_sha3_512  ();
    case Algorithm::shake128   : return EVP_shake128  ();
    case Algorithm::shake256   : return EVP_shake256  ();
    case Algorithm::mdc2       : return EVP_mdc2      ();
    case Algorithm::ripemd160  : return EVP_ripemd160 ();
    case Algorithm::whirlpool  : return EVP_whirlpool ();
    case Algorithm::sm3        : return EVP_sm3       ();
  }
  return nullptr;
}

Hash::Hash(Algorithm algorithm) {
  m_ctx = EVP_MD_CTX_new();
  EVP_DigestInit_ex(m_ctx, digest(algorithm), nullptr);
}

Hash::~Hash() {
  EVP_MD_CTX_free(m_ctx);
}

void Hash::update(Data *data) {
  for (const auto &c : data->chunks()) {
    auto ptr = std::get<0>(c);
    auto len = std::get<1>(c);
    EVP_DigestUpdate(m_ctx, (unsigned char *)ptr, len);
  }
}

void Hash::update(pjs::Str *str, Data::Encoding enc) {
  switch (enc) {
    case Data::Encoding::UTF8:
      EVP_DigestUpdate(m_ctx, (unsigned char *)str->c_str(), str->length());
      break;
    default: {
      Data data(str->str(), enc, &s_dp_hash);
      update(&data);
      break;
    }
  }
}

auto Hash::digest() -> Data* {
  char hash[EVP_MAX_MD_SIZE];
  unsigned int size;
  EVP_DigestFinal_ex(m_ctx, (unsigned char *)hash, &size);
  return s_dp_hash.make(hash, size);
}

auto Hash::digest(Data::Encoding enc) -> pjs::Str* {
  char hash[EVP_MAX_MD_SIZE];
  unsigned int size;
  EVP_DigestFinal_ex(m_ctx, (unsigned char *)hash, &size);
  switch (enc) {
    case Data::Encoding::UTF8:
      return pjs::Str::make(hash, size);
    case Data::Encoding::Hex: {
      char str[size * 2];
      auto len = utils::encode_hex(str, hash, size);
      return pjs::Str::make(str, len);
    }
    case Data::Encoding::Base64: {
      char str[size * 2];
      auto len = utils::encode_base64(str, hash, size);
      return pjs::Str::make(str, len);
    }
    case Data::Encoding::Base64Url: {
      char str[size * 2];
      auto len = utils::encode_base64url(str, hash, size);
      return pjs::Str::make(str, len);
    }
  }
  return nullptr;
}

//
// Hmac
//

Hmac::Hmac(Hash::Algorithm algorithm, Data *key) {
  m_ctx = HMAC_CTX_new();
  auto buf = key->to_bytes();
  HMAC_Init_ex(m_ctx, &buf[0], buf.size(), Hash::digest(algorithm), nullptr);
}

Hmac::Hmac(Hash::Algorithm algorithm, pjs::Str *key) {
  m_ctx = HMAC_CTX_new();
  HMAC_Init_ex(m_ctx, key->c_str(), key->length(), Hash::digest(algorithm), nullptr);
}

Hmac::~Hmac() {
  HMAC_CTX_free(m_ctx);
}

void Hmac::update(Data *data) {
  for (const auto &c : data->chunks()) {
    auto ptr = std::get<0>(c);
    auto len = std::get<1>(c);
    HMAC_Update(m_ctx, (unsigned char *)ptr, len);
  }
}

void Hmac::update(pjs::Str *str, Data::Encoding enc) {
  switch (enc) {
    case Data::Encoding::UTF8:
      HMAC_Update(m_ctx, (unsigned char *)str->c_str(), str->length());
      break;
    default: {
      Data data(str->str(), enc, &s_dp_hmac);
      update(&data);
      break;
    }
  }
}

auto Hmac::digest() -> Data* {
  char hash[EVP_MAX_MD_SIZE];
  unsigned int size;
  HMAC_Final(m_ctx, (unsigned char *)hash, &size);
  return s_dp_hmac.make(hash, size);
}

auto Hmac::digest(Data::Encoding enc) -> pjs::Str* {
  char hash[EVP_MAX_MD_SIZE];
  unsigned int size;
  HMAC_Final(m_ctx, (unsigned char *)hash, &size);
  switch (enc) {
    case Data::Encoding::UTF8:
      return pjs::Str::make(hash, size);
    case Data::Encoding::Hex: {
      char str[size * 2];
      auto len = utils::encode_hex(str, hash, size);
      return pjs::Str::make(str, len);
    }
    case Data::Encoding::Base64: {
      char str[size * 2];
      auto len = utils::encode_base64(str, hash, size);
      return pjs::Str::make(str, len);
    }
    case Data::Encoding::Base64Url: {
      char str[size * 2];
      auto len = utils::encode_base64url(str, hash, size);
      return pjs::Str::make(str, len);
    }
  }
  return nullptr;
}

//
// Sign
//

Sign::Sign(Hash::Algorithm algorithm) {
  m_md = Hash::digest(algorithm);
  m_ctx = EVP_MD_CTX_new();
  if (!m_ctx) throw_error();
  if (!EVP_DigestInit_ex(m_ctx, m_md, nullptr)) throw_error();
}

Sign::~Sign() {
  EVP_MD_CTX_free(m_ctx);
}

void Sign::update(Data *data) {
  for (const auto &c : data->chunks()) {
    auto ptr = std::get<0>(c);
    auto len = std::get<1>(c);
    if (!EVP_DigestUpdate(m_ctx, (unsigned char *)ptr, len)) throw_error();
  }
}

void Sign::update(pjs::Str *str, Data::Encoding enc) {
  switch (enc) {
    case Data::Encoding::UTF8:
      if (!EVP_DigestUpdate(m_ctx, (unsigned char *)str->c_str(), str->length())) throw_error();
      break;
    default: {
      Data data(str->str(), enc, &s_dp_sign);
      update(&data);
      break;
    }
  }
}

auto Sign::sign(PrivateKey *key, Object *options) -> Data* {
  unsigned char hash[EVP_MAX_MD_SIZE];
  unsigned int size;
  if (!EVP_DigestFinal_ex(m_ctx, hash, &size)) throw_error();

  auto ctx = EVP_PKEY_CTX_new(key->pkey(), nullptr);
  if (!ctx) throw_error();
  if (EVP_PKEY_sign_init(ctx) <= 0) throw_error();
  if (EVP_PKEY_CTX_set_signature_md(ctx, m_md) <= 0) throw_error();

  EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_PADDING);
  set_pkey_ctx_options(ctx, options);

  size_t sig_len;
  if (EVP_PKEY_sign(ctx, nullptr, &sig_len, hash, size) <= 0) throw_error();
  unsigned char sig[sig_len];
  if (EVP_PKEY_sign(ctx, sig, &sig_len, hash, size) <= 0) throw_error();

  EVP_PKEY_CTX_free(ctx);
  return s_dp_sign.make(&sig[0], sig_len);
}

auto Sign::sign(PrivateKey *key, Data::Encoding enc, Object *options) -> pjs::Str* {
  pjs::Ref<Data> data = sign(key, options);
  return pjs::Str::make(data->to_string(enc));
}

//
// Verify
//

Verify::Verify(Hash::Algorithm algorithm) {
  m_md = Hash::digest(algorithm);
  m_ctx = EVP_MD_CTX_new();
  if (!m_ctx) throw_error();
  if (!EVP_DigestInit_ex(m_ctx, m_md, nullptr)) throw_error();
}

Verify::~Verify() {
  EVP_MD_CTX_free(m_ctx);
}

void Verify::update(Data *data) {
  for (const auto &c : data->chunks()) {
    auto ptr = std::get<0>(c);
    auto len = std::get<1>(c);
    if (!EVP_DigestUpdate(m_ctx, (unsigned char *)ptr, len)) throw_error();
  }
}

void Verify::update(pjs::Str *str, Data::Encoding enc) {
  switch (enc) {
    case Data::Encoding::UTF8:
      if (!EVP_DigestUpdate(m_ctx, (unsigned char *)str->c_str(), str->length())) throw_error();
      break;
    default: {
      Data data(str->str(), enc, &s_dp_verify);
      update(&data);
      break;
    }
  }
}

bool Verify::verify(PublicKey *key, Data *signature, Object *options) {
  unsigned char hash[EVP_MAX_MD_SIZE];
  unsigned int size;
  if (!EVP_DigestFinal_ex(m_ctx, hash, &size)) throw_error();

  auto ctx = EVP_PKEY_CTX_new(key->pkey(), nullptr);
  if (!ctx) throw_error();
  if (EVP_PKEY_verify_init(ctx) <= 0) throw_error();
  if (EVP_PKEY_CTX_set_signature_md(ctx, m_md) < 0) throw_error();

  EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_PADDING);
  set_pkey_ctx_options(ctx, options);

  auto sig = signature->to_bytes();
  auto result = EVP_PKEY_verify(ctx, &sig[0], sig.size(), hash, size);
  EVP_PKEY_CTX_free(ctx);

  if (result < 0) throw_error();
  return result == 1;
}

bool Verify::verify(PublicKey *key, pjs::Str *signature, Data::Encoding enc, Object *options) {
  Data sig(signature->str(), enc, &s_dp_verify);
  return verify(key, &sig, options);
}

//
// JWK
//

JWK::JWK(pjs::Object *json) {
  pjs::Value kty, crv, n, e, x, y;
  json->get("kty", kty);
  if (!kty.is_string()) throw std::runtime_error("missing \"kty\"");
  if (kty.s()->str() == "RSA") {
    json->get("n", n);
    json->get("e", e);
    if (!n.is_string()) throw std::runtime_error("missing \"n\"");
    if (!e.is_string()) throw std::runtime_error("missing \"e\"");
    const auto &n_str = n.s()->str();
    const auto &e_str = e.s()->str();
    char n_bin[n_str.length() * 2];
    char e_bin[e_str.length() * 2];
    auto n_len = utils::decode_base64url(n_bin, n_str.c_str(), n_str.length());
    auto e_len = utils::decode_base64url(e_bin, e_str.c_str(), e_str.length());
    if (n_len < 0) throw std::runtime_error("invalid \"n\"");
    if (e_len < 0) throw std::runtime_error("invalid \"e\"");
    auto n_num = BN_bin2bn((unsigned char *)n_bin, n_len, nullptr);
    auto e_num = BN_bin2bn((unsigned char *)e_bin, e_len, nullptr);
    auto rsa = RSA_new();
    RSA_set0_key(rsa, n_num, e_num, nullptr);
    m_pkey = EVP_PKEY_new();
    EVP_PKEY_assign_RSA(m_pkey, rsa);

  } else if (kty.s()->str() == "EC") {
    json->get("crv", crv);
    json->get("x", x);
    json->get("y", y);
    if (!crv.is_string()) throw std::runtime_error("missing \"crv\"");
    if (!x.is_string()) throw std::runtime_error("missing \"x\"");
    if (!y.is_string()) throw std::runtime_error("missing \"y\"");
    auto nid = EC_curve_nist2nid(crv.s()->c_str());
    if (nid == NID_undef) throw std::runtime_error("unknown \"crv\"");
    const auto &x_str = x.s()->str();
    const auto &y_str = y.s()->str();
    char x_bin[x_str.length() * 2];
    char y_bin[y_str.length() * 2];
    auto x_len = utils::decode_base64url(x_bin, x_str.c_str(), x_str.length());
    auto y_len = utils::decode_base64url(y_bin, y_str.c_str(), y_str.length());
    if (x_len < 0) throw std::runtime_error("invalid \"x\"");
    if (y_len < 0) throw std::runtime_error("invalid \"y\"");
    auto x_num = BN_bin2bn((unsigned char *)x_bin, x_len, nullptr);
    auto y_num = BN_bin2bn((unsigned char *)y_bin, y_len, nullptr);
    auto ec = EC_KEY_new_by_curve_name(nid);
    EC_KEY_set_public_key_affine_coordinates(ec, x_num, y_num);
    m_pkey = EVP_PKEY_new();
    EVP_PKEY_assign_EC_KEY(m_pkey, ec);

  } else {
    throw std::runtime_error("unknown \"kty\"");
  }
}

JWK::~JWK() {
  if (m_pkey) {
    EVP_PKEY_free(m_pkey);
  }
}

//
// JWT
//

JWT::JWT(pjs::Str *token) {
  auto segs = utils::split(token->str(), '.');
  if (segs.size() != 3) return;

  auto i = segs.begin();
  m_header_str = *i++;
  m_payload_str = *i++;
  m_signature_str = *i;

  char buf1[m_header_str.length() * 2];
  char buf2[m_payload_str.length() * 2];
  char buf3[m_signature_str.length() * 2];
  auto len1 = utils::decode_base64url(buf1, m_header_str.c_str(), m_header_str.length());
  auto len2 = utils::decode_base64url(buf2, m_payload_str.c_str(), m_payload_str.length());
  auto len3 = utils::decode_base64url(buf3, m_signature_str.c_str(), m_signature_str.length());

  if (len1 < 0 || len2 < 0 || len3 < 0) return;
  if (!JSON::parse(std::string(buf1, len1), m_header)) return;
  if (!JSON::parse(std::string(buf2, len2), m_payload)) return;

  if (!m_header.is_object() || m_header.is_null()) return;
  if (!m_payload.is_object() || m_payload.is_null()) return;

  pjs::Value alg;
  m_header.o()->get("alg", alg);
  if (!alg.is_string()) return;
  auto algorithm = pjs::EnumDef<Algorithm>::value(alg.s());
  if (int(algorithm) < 0) return;
  m_algorithm = algorithm;

  switch (algorithm) {
    case Algorithm::ES256:
    case Algorithm::ES384:
    case Algorithm::ES512: {
      char out[len3 * 2];
      auto len = jose2der(out, buf3, len3);
      m_signature = std::string(out, len);
      break;
    }
    default: {
      m_signature = std::string(buf3, len3);
      break;
    }
  }

  m_is_valid = true;
}

JWT::~JWT() {
}

void JWT::sign(pjs::Str *key) {
  throw std::runtime_error("TODO");
}

bool JWT::verify(Data *key) {
  auto buf = key->to_bytes();
  return verify((const char *)&buf[0], buf.size());
}

bool JWT::verify(pjs::Str *key) {
  return verify(key->c_str(), key->length());
}

bool JWT::verify(JWK *key) {
  if (!key || !key->is_valid()) return false;
  return verify(key->pkey());
}

bool JWT::verify(PrivateKey *key) {
  return verify(key->pkey());
}

auto JWT::get_md() -> const EVP_MD* {
  switch (m_algorithm) {
    case Algorithm::HS256: return EVP_sha256();
    case Algorithm::HS384: return EVP_sha384();
    case Algorithm::HS512: return EVP_sha512();
    case Algorithm::RS256: return EVP_sha256();
    case Algorithm::RS384: return EVP_sha384();
    case Algorithm::RS512: return EVP_sha512();
    case Algorithm::ES256: return EVP_sha256();
    case Algorithm::ES384: return EVP_sha384();
    case Algorithm::ES512: return EVP_sha512();
  }
  return nullptr;
}

bool JWT::verify(const char *key, int key_len) {
  if (!m_is_valid) return false;

  if (m_algorithm == Algorithm::HS256 ||
      m_algorithm == Algorithm::HS384 ||
      m_algorithm == Algorithm::HS512
  ) {
    auto md = get_md();
    if (!md) return false;

    auto ctx = HMAC_CTX_new();
    char sep = '.';
    HMAC_Init_ex(ctx, key, key_len, md, nullptr);
    HMAC_Update(ctx, (unsigned char *)m_header_str.c_str(), m_header_str.length());
    HMAC_Update(ctx, (unsigned char *)&sep, 1);
    HMAC_Update(ctx, (unsigned char *)m_payload_str.c_str(), m_payload_str.length());

    char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_size;
    HMAC_Final(ctx, (unsigned char *)hash, &hash_size);
    HMAC_CTX_free(ctx);

    if (hash_size != m_signature.length()) return false;
    return std::memcmp(m_signature.c_str(), hash, hash_size) == 0;

  } else {
    auto bio = BIO_new_mem_buf(key, key_len);
    auto pkey = PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr);
    if (!pkey) {
      BIO_reset(bio);
      pkey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
    }
    BIO_free(bio);
    if (!pkey) throw_error();

    auto result = verify(pkey);
    EVP_PKEY_free(pkey);
    return result;
  }
}

bool JWT::verify(EVP_PKEY *pkey) {
  auto md = get_md();
  if (!md) return false;

  unsigned char hash[EVP_MAX_MD_SIZE];
  unsigned int hash_size;

  auto mdctx = EVP_MD_CTX_new();
  EVP_DigestInit_ex(mdctx, md, nullptr);
  EVP_DigestUpdate(mdctx, m_header_str.c_str(), m_header_str.length());
  EVP_DigestUpdate(mdctx, ".", 1);
  EVP_DigestUpdate(mdctx, m_payload_str.c_str(), m_payload_str.length());
  EVP_DigestFinal_ex(mdctx, hash, &hash_size);

  auto pctx = EVP_PKEY_CTX_new(pkey, nullptr);
  EVP_PKEY_verify_init(pctx);
  EVP_PKEY_CTX_set_rsa_padding(pctx, RSA_PKCS1_PADDING);
  EVP_PKEY_CTX_set_signature_md(pctx, md);

  auto result = EVP_PKEY_verify(
    pctx,
    (unsigned char *)m_signature.c_str(),
    m_signature.length(),
    hash, hash_size);

  EVP_PKEY_CTX_free(pctx);
  EVP_MD_CTX_free(mdctx);

  return result == 1;
}

int JWT::jose2der(char *out, const char *inp, int len) {
  auto width = len >> 1;
  int zero_r = 0; while (zero_r < width && !inp[zero_r]) zero_r++;
  int zero_s = 0; while (zero_s < width && !inp[zero_s + width]) zero_s++;
  if (0x80 <= (unsigned char)inp[zero_r]) --zero_r;
  if (0x80 <= (unsigned char)inp[zero_s + width]) --zero_s;
  auto size_r = width - zero_r;
  auto size_s = width - zero_s;
  auto size_rs = size_r + size_s + 4;

  int i = 0;
  out[i++] = 0x30; // SEQ
  if (size_rs < 0x80) {
    out[i++] = size_rs;
  } else {
    out[i++] = 0x81;
    out[i++] = size_rs & 0xff;
  }

  out[i++] = 0x02, // INT
  out[i++] = size_r;
  if (zero_r >= 0) {
    std::memcpy(out + i, inp + zero_r, size_r);
  } else {
    out[i++] = 0;
    std::memcpy(out + i, inp, width);
  }
  i += size_r;

  out[i++] = 0x02, // INT
  out[i++] = size_s;
  if (zero_s >= 0) {
    std::memcpy(out + i, inp + width + zero_s, size_s);
  } else {
    out[i++] = 0;
    std::memcpy(out + i, inp + width, width);
  }
  i += size_r;
  return i;
}

} // namespace crypto
} // namespace pipy

namespace pjs {

using namespace pipy::crypto;

//
// Options
//

template<> void EnumDef<Options>::init() {
  define(Options::alias_type, "aliasType");
  define(Options::id, "id");
  define(Options::key, "key");
  define(Options::iv, "iv");
}

//
// AliasType
//

template<> void EnumDef<AliasType>::init() {
  define(AliasType::sm2, "sm2");
}

//
// PublicKey
//

template<> void ClassDef<PublicKey>::init() {
  ctor([](Context &ctx) -> Object* {
    Str *data_str;
    pipy::Data *data;
    Object *options = nullptr;
    try {
      if (ctx.try_arguments(1, &data_str, &options)) {
        return PublicKey::make(data_str, options);
      } else if (ctx.arguments(1, &data, &options)) {
        return PublicKey::make(data, options);
      } else {
        return nullptr;
      }
    } catch (std::runtime_error &err) {
      ctx.error(err);
      return nullptr;
    }
  });
}

template<> void ClassDef<Constructor<PublicKey>>::init() {
  super<Function>();
  ctor();
}

//
// PrivateKey
//

template<> void ClassDef<PrivateKey>::init() {
  ctor([](Context &ctx) -> Object* {
    Str *data_str;
    pipy::Data *data;
    Object *options = nullptr;
    try {
      if (ctx.try_arguments(1, &data_str, &options)) {
        return PrivateKey::make(data_str, options);
      } else if (ctx.arguments(1, &data, &options)) {
        return PrivateKey::make(data, options);
      } else {
        return nullptr;
      }
    } catch (std::runtime_error &err) {
      ctx.error(err);
      return nullptr;
    }
  });
}

template<> void ClassDef<Constructor<PrivateKey>>::init() {
  super<Function>();
  ctor();
}

//
// Certificate
//

template<> void ClassDef<Certificate>::init() {
  ctor([](Context &ctx) -> Object* {
    Str *data_str;
    pipy::Data *data;
    try {
      if (ctx.try_arguments(1, &data_str)) {
        return Certificate::make(data_str);
      } else if (ctx.arguments(1, &data)) {
        return Certificate::make(data);
      } else {
        return nullptr;
      }
    } catch (std::runtime_error &err) {
      ctx.error(err);
      return nullptr;
    }
  });
}

template<> void ClassDef<Constructor<Certificate>>::init() {
  super<Function>();
  ctor();
}

//
// Cipher
//

template<> void EnumDef<Cipher::Algorithm>::init() {
  define(Cipher::Algorithm::sm4_cbc, "sm4-cbc");
  define(Cipher::Algorithm::sm4_ecb, "sm4-ecb");
  define(Cipher::Algorithm::sm4_cfb, "sm4-cfb");
  define(Cipher::Algorithm::sm4_cfb128, "sm4-cfb128");
  define(Cipher::Algorithm::sm4_ofb, "sm4-ofb");
  define(Cipher::Algorithm::sm4_ctr, "sm4-ctr");
}

template<> void ClassDef<Cipher>::init() {
  ctor([](Context &ctx) -> Object* {
    Str *algorithm_name;
    Object *options;
    if (!ctx.arguments(2, &algorithm_name, &options)) return nullptr;
    if (!options) {
      ctx.error("options cannot be null");
      return nullptr;
    }
    auto algorithm = EnumDef<Cipher::Algorithm>::value(algorithm_name);
    if (int(algorithm) < 0) {
      ctx.error("unknown cipher");
      return nullptr;
    }
    try {
      return Cipher::make(algorithm, options);
    } catch (std::runtime_error &err) {
      ctx.error(err);
      return nullptr;
    }
  });

  method("update", [](Context &ctx, Object *obj, Value &ret) {
    Str *str;
    pipy::Data *data;
    try {
      if (ctx.try_arguments(1, &str)) {
        ret.set(obj->as<Cipher>()->update(str));
      } else if (ctx.try_arguments(1, &data)) {
        ret.set(obj->as<Cipher>()->update(data));
      } else {
        ctx.error_argument_type(0, "a Data object or a string");
      }
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  method("final", [](Context &ctx, Object *obj, Value &ret) {
    try {
      ret.set(obj->as<Cipher>()->final());
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });
}

template<> void ClassDef<Constructor<Cipher>>::init() {
  super<Function>();
  ctor();
}

//
// Decipher
//

template<> void ClassDef<Decipher>::init() {
  ctor([](Context &ctx) -> Object* {
    Str *algorithm_name;
    Object *options;
    if (!ctx.arguments(2, &algorithm_name, &options)) return nullptr;
    if (!options) {
      ctx.error("options cannot be null");
      return nullptr;
    }
    auto algorithm = EnumDef<Cipher::Algorithm>::value(algorithm_name);
    if (int(algorithm) < 0) {
      ctx.error("unknown cipher");
      return nullptr;
    }
    try {
      return Decipher::make(algorithm, options);
    } catch (std::runtime_error &err) {
      ctx.error(err);
      return nullptr;
    }
  });

  method("update", [](Context &ctx, Object *obj, Value &ret) {
    Str *str;
    pipy::Data *data;
    try {
      if (ctx.try_arguments(1, &str)) {
        ret.set(obj->as<Decipher>()->update(str));
      } else if (ctx.try_arguments(1, &data)) {
        ret.set(obj->as<Decipher>()->update(data));
      } else {
        ctx.error_argument_type(0, "a Data object or a string");
      }
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  method("final", [](Context &ctx, Object *obj, Value &ret) {
    try {
      ret.set(obj->as<Decipher>()->final());
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });
}

template<> void ClassDef<Constructor<Decipher>>::init() {
  super<Function>();
  ctor();
}

//
// Hash
//

template<> void EnumDef<Hash::Algorithm>::init() {
  define(Hash::Algorithm::md4       , "md4");
  define(Hash::Algorithm::md5       , "md5");
  define(Hash::Algorithm::md5_sha1  , "md5-sha1");
  define(Hash::Algorithm::blake2b512, "blake2b512");
  define(Hash::Algorithm::blake2s256, "blake2s256");
  define(Hash::Algorithm::sha1      , "sha1");
  define(Hash::Algorithm::sha224    , "sha224");
  define(Hash::Algorithm::sha256    , "sha256");
  define(Hash::Algorithm::sha384    , "sha384");
  define(Hash::Algorithm::sha512    , "sha512");
  define(Hash::Algorithm::sha512_224, "sha512-224");
  define(Hash::Algorithm::sha512_256, "sha512-256");
  define(Hash::Algorithm::sha3_224  , "sha3-224");
  define(Hash::Algorithm::sha3_256  , "sha3-256");
  define(Hash::Algorithm::sha3_384  , "sha3-384");
  define(Hash::Algorithm::sha3_512  , "sha3-512");
  define(Hash::Algorithm::shake128  , "shake128");
  define(Hash::Algorithm::shake256  , "shake256");
  define(Hash::Algorithm::mdc2      , "mdc2");
  define(Hash::Algorithm::ripemd160 , "ripemd160");
  define(Hash::Algorithm::whirlpool , "whirlpool");
  define(Hash::Algorithm::sm3       , "sm3");
}

template<> void ClassDef<Hash>::init() {
  ctor([](Context &ctx) -> Object* {
    Str *algorithm_name;
    if (!ctx.arguments(1, &algorithm_name)) return nullptr;
    auto algorithm = EnumDef<Hash::Algorithm>::value(algorithm_name);
    if (int(algorithm) < 0) {
      ctx.error("unknown message digest");
      return nullptr;
    }
    return Hash::make(algorithm);
  });

  method("update", [](Context &ctx, Object *obj, Value &ret) {
    Object *data;
    Str *str, *encoding_name = nullptr;
    if (ctx.try_arguments(1, &data) && data && data->is<pipy::Data>()) {
      obj->as<Hash>()->update(data->as<pipy::Data>());
    } else if (ctx.try_arguments(1, &str, &encoding_name)) {
      auto encoding = EnumDef<pipy::Data::Encoding>::value(encoding_name, pipy::Data::Encoding::UTF8);
      if (int(encoding) < 0) {
        ctx.error("unknown encoding");
      } else {
        obj->as<Hash>()->update(str, encoding);
      }
    } else {
      ctx.error_argument_type(0, "a Data object or a string");
    }
    ret = Value::undefined;
  });

  method("digest", [](Context &ctx, Object *obj, Value &ret) {
    Str *encoding_name = nullptr;
    if (!ctx.arguments(0, &encoding_name)) return;
    if (encoding_name) {
      auto encoding = EnumDef<pipy::Data::Encoding>::value(encoding_name);
      if (int(encoding) < 0) {
        ctx.error("unknown encoding");
      } else {
        ret.set(obj->as<Hash>()->digest(encoding));
      }
    } else {
      ret.set(obj->as<Hash>()->digest());
    }
  });
}

template<> void ClassDef<Constructor<Hash>>::init() {
  super<Function>();
  ctor();
}

//
// Hmac
//

template<> void ClassDef<Hmac>::init() {
  ctor([](Context &ctx) -> Object* {
    Str *algorithm_name, *key_str = nullptr;
    pipy::Data *key = nullptr;
    if (ctx.try_arguments(2, &algorithm_name, &key) ||
        ctx.try_arguments(2, &algorithm_name, &key_str))
    {
      auto algorithm = EnumDef<Hash::Algorithm>::value(algorithm_name);
      if (int(algorithm) < 0) {
        ctx.error("unknown message digest");
        return nullptr;
      }
      if (key) {
        return Hmac::make(algorithm, key);
      } else if (key_str) {
        return Hmac::make(algorithm, key_str);
      } else {
        ctx.error_argument_type(1, "a Data object or a string");
      }
    }
    return nullptr;
  });

  method("update", [](Context &ctx, Object *obj, Value &ret) {
    pipy::Data *data;
    Str *str, *encoding_name = nullptr;
    if (ctx.try_arguments(1, &data)) {
      obj->as<Hmac>()->update(data);
    } else if (ctx.try_arguments(1, &str, &encoding_name)) {
      auto encoding = EnumDef<pipy::Data::Encoding>::value(encoding_name, pipy::Data::Encoding::UTF8);
      if (int(encoding) < 0) {
        ctx.error("unknown encoding");
      } else {
        obj->as<Hmac>()->update(str, encoding);
      }
    } else {
      ctx.error_argument_type(0, "a Data object or a string");
    }
    ret = Value::undefined;
  });

  method("digest", [](Context &ctx, Object *obj, Value &ret) {
    Str *encoding_name = nullptr;
    if (!ctx.arguments(0, &encoding_name)) return;
    if (encoding_name) {
      auto encoding = EnumDef<pipy::Data::Encoding>::value(encoding_name);
      if (int(encoding) < 0) {
        ctx.error("unknown encoding");
      } else {
        ret.set(obj->as<Hmac>()->digest(encoding));
      }
    } else {
      ret.set(obj->as<Hmac>()->digest());
    }
  });
}

template<> void ClassDef<Constructor<Hmac>>::init() {
  super<Function>();
  ctor();
}

//
// Sign
//

template<> void ClassDef<Sign>::init() {
  ctor([](Context &ctx) -> Object* {
    Str *algorithm_name;
    if (!ctx.arguments(1, &algorithm_name)) return nullptr;
    auto algorithm = EnumDef<Hash::Algorithm>::value(algorithm_name);
    if (int(algorithm) < 0) {
      ctx.error("unknown message digest");
      return nullptr;
    }
    try {
      return Sign::make(algorithm);
    } catch (std::runtime_error &err) {
      ctx.error(err);
      return nullptr;
    }
  });

  method("update", [](Context &ctx, Object *obj, Value &ret) {
    pipy::Data *data;
    Str *str, *encoding_name = nullptr;
    try {
      if (ctx.try_arguments(1, &data)) {
        obj->as<Sign>()->update(data);
      } else if (ctx.try_arguments(1, &str, &encoding_name)) {
        auto encoding = EnumDef<pipy::Data::Encoding>::value(encoding_name, pipy::Data::Encoding::UTF8);
        if (int(encoding) < 0) {
          ctx.error("unknown encoding");
        } else {
          obj->as<Sign>()->update(str, encoding);
        }
      } else {
        ctx.error_argument_type(0, "a Data object or a string");
      }
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  method("sign", [](Context &ctx, Object *obj, Value &ret) {
    PrivateKey *key;
    Str *encoding_name = nullptr;
    Object *options = nullptr;
    try {
      if (ctx.try_arguments(1, &key, &options) ||
          ctx.try_arguments(1, &key, &encoding_name, &options)
      ) {
        if (!key) {
          ctx.error_argument_type(0, "a PrivateKey object");
          return;
        }
        auto encoding = EnumDef<pipy::Data::Encoding>::value(encoding_name, pipy::Data::Encoding::UTF8);
        if (int(encoding) < 0) {
          ctx.error("unknown encoding");
          return;
        }
        try {
          if (encoding_name) {
            ret.set(obj->as<Sign>()->sign(key, encoding, options));
          } else {
            ret.set(obj->as<Sign>()->sign(key, options));
          }
        } catch(std::runtime_error &err) {
          ctx.error(err);
        }
      } else if (ctx.arguments(1, &key)) {
        ctx.error_argument_type(1, "a object or a string");
      }
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });
}

template<> void ClassDef<Constructor<Sign>>::init() {
  super<Function>();
  ctor();
}

//
// Verify
//

template<> void ClassDef<Verify>::init() {
  ctor([](Context &ctx) -> Object* {
    Str *algorithm_name;
    if (!ctx.arguments(1, &algorithm_name)) return nullptr;
    auto algorithm = EnumDef<Hash::Algorithm>::value(algorithm_name);
    if (int(algorithm) < 0) {
      ctx.error("unknown message digest");
      return nullptr;
    }
    try {
      return Verify::make(algorithm);
    } catch (std::runtime_error &err) {
      ctx.error(err);
      return nullptr;
    }
  });

  method("update", [](Context &ctx, Object *obj, Value &ret) {
    pipy::Data *data;
    Str *str, *encoding_name = nullptr;
    try {
      if (ctx.try_arguments(1, &data)) {
        obj->as<Verify>()->update(data);
      } else if (ctx.try_arguments(1, &str, &encoding_name)) {
        auto encoding = EnumDef<pipy::Data::Encoding>::value(encoding_name, pipy::Data::Encoding::UTF8);
        if (int(encoding) < 0) {
          ctx.error("unknown encoding");
        } else {
          obj->as<Verify>()->update(str, encoding);
        }
      } else {
        ctx.error_argument_type(0, "a Data object or a string");
      }
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  method("verify", [](Context &ctx, Object *obj, Value &ret) {
    PublicKey *key;
    Str *signature_str = nullptr, *encoding_name = nullptr;
    pipy::Data *signature = nullptr;
    Object *options = nullptr;
    try {
      if (ctx.try_arguments(2, &key, &signature, &options) ||
          ctx.try_arguments(2, &key, &signature_str, &encoding_name, &options)
      ) {
        if (!key) {
          ctx.error_argument_type(0, "a PublicKey object");
          return;
        }
        auto encoding = EnumDef<pipy::Data::Encoding>::value(encoding_name, pipy::Data::Encoding::UTF8);
        if (int(encoding) < 0) {
          ctx.error("unknown encoding");
          return;
        }
        try {
          if (signature) {
            ret.set(obj->as<Verify>()->verify(key, signature, options));
          } else {
            ret.set(obj->as<Verify>()->verify(key, signature_str, encoding, options));
          }
        } catch(std::runtime_error &err) {
          ctx.error(err);
        }
      } else if (ctx.arguments(1, &key)) {
        ctx.error_argument_type(1, "a Data object or a string");
      }
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });
}

template<> void ClassDef<Constructor<Verify>>::init() {
  super<Function>();
  ctor();
}

//
// JWK
//

template<> void ClassDef<JWK>::init() {
  ctor([](Context &ctx) -> Object* {
    Object *json;
    if (!ctx.arguments(1, &json)) return nullptr;
    if (!json) {
      ctx.error_argument_type(0, "a non-null object");
      return nullptr;
    }
    try {
      return JWK::make(json);
    } catch (std::runtime_error &err) {
      ctx.error(err);
      return nullptr;
    }
  });

  accessor("isValid", [](Object *obj, Value &ret) { ret.set(obj->as<JWK>()->is_valid()); });
}

template<> void ClassDef<Constructor<JWK>>::init() {
  super<Function>();
  ctor();
}

//
// JWT
//

template<> void EnumDef<JWT::Algorithm>::init() {
  define(JWT::Algorithm::HS256, "HS256");
  define(JWT::Algorithm::HS384, "HS384");
  define(JWT::Algorithm::HS512, "HS512");
  define(JWT::Algorithm::RS256, "RS256");
  define(JWT::Algorithm::RS384, "RS384");
  define(JWT::Algorithm::RS512, "RS512");
  define(JWT::Algorithm::ES256, "ES256");
  define(JWT::Algorithm::ES384, "ES384");
  define(JWT::Algorithm::ES512, "ES512");
}

template<> void ClassDef<JWT>::init() {
  ctor([](Context &ctx) -> Object* {
    Str *token;
    if (!ctx.arguments(1, &token)) return nullptr;
    return JWT::make(token);
  });

  accessor("isValid", [](Object *obj, Value &ret) { ret.set(obj->as<JWT>()->is_valid()); });
  accessor("header", [](Object *obj, Value &ret) { ret = obj->as<JWT>()->header(); });
  accessor("payload", [](Object *obj, Value &ret) { ret = obj->as<JWT>()->payload(); });

  method("verify", [](Context &ctx, Object *obj, Value &ret) {
    pipy::Data *data = nullptr;
    Str *str = nullptr;
    JWK *jwk = nullptr;
    PrivateKey *pkey = nullptr;
    if (ctx.try_arguments(1, &data) ||
        ctx.try_arguments(1, &str) ||
        ctx.try_arguments(1, &jwk) ||
        ctx.try_arguments(1, &pkey)
    ) {
      try {
        if (data) {
          ret.set(obj->as<JWT>()->verify(data));
        } else if (str) {
          ret.set(obj->as<JWT>()->verify(str));
        } else if (jwk) {
          ret.set(obj->as<JWT>()->verify(jwk));
        } else if (pkey) {
          ret.set(obj->as<JWT>()->verify(pkey));
        }
      } catch (std::runtime_error &err) {
        ctx.error(err);
      }
    } else {
      ctx.error_argument_type(0, "a Data object or a string or a private key object");
    }
  });
}

template<> void ClassDef<Constructor<JWT>>::init() {
  super<Function>();
  ctor();
}

//
// Crypto
//

template<> void ClassDef<Crypto>::init() {
  ctor();
  variable("PublicKey", class_of<Constructor<PublicKey>>());
  variable("PrivateKey", class_of<Constructor<PrivateKey>>());
  variable("Certificate", class_of<Constructor<Certificate>>());
  variable("Cipher", class_of<Constructor<Cipher>>());
  variable("Decipher", class_of<Constructor<Decipher>>());
  variable("Hash", class_of<Constructor<Hash>>());
  variable("Hmac", class_of<Constructor<Hmac>>());
  variable("Sign", class_of<Constructor<Sign>>());
  variable("Verify", class_of<Constructor<Verify>>());
  variable("JWT", class_of<Constructor<JWT>>());
  variable("JWK", class_of<Constructor<JWK>>());
}

} // namespace pjs