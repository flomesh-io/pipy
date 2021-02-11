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

#include <openssl/bio.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <string>

NS_BEGIN

using namespace crypto;

DEFINE_CLASS(Sign);
DEFINE_SYMBOLS(Sign::Algorithm);
DEFINE_SYMBOLS(Sign::Option);

DEFINE_CLASS(Verify);
DEFINE_SYMBOLS(Verify::Algorithm);
DEFINE_SYMBOLS(Verify::Option);

DEFINE_CLASS(Cipher);
DEFINE_SYMBOLS(Cipher::Algorithm);

DEFINE_CLASS(Decipher);
DEFINE_SYMBOLS(Decipher::Algorithm);

namespace crypto {

  void Sign::define(JSContext *ctx) {
    js::Symbols<Algorithm> s1(ctx);
    js::Symbols<Option> s2(ctx);
    s1.DEFINE_SYMBOL_NAME(Algorithm, sm2_sm3, "sm2-sm3");
    s1.DEFINE_SYMBOL_NAME(Algorithm, sm2_sha256, "sm2-sha256");
    s2.DEFINE_SYMBOL(Option, key);
    s2.DEFINE_SYMBOL(Option, id);
    define_class(ctx);
    define_ctor(ctx, construct, 2, "crypto");
    DEFINE_FUNC(ctx, update, 1);
    DEFINE_FUNC(ctx, final, 0);
  }

  Sign::Sign() {
  }

  Sign::~Sign() {
    EVP_MD_CTX_free(mctx);
    EVP_PKEY_CTX_free(pctx);
    EVP_PKEY_free(pkey);
  }

  auto Sign::construct(JSContext *ctx, JSValueConst this_obj, int argc, JSValueConst *argv) -> JSValue {
    js::Symbols<Algorithm> S(ctx);
    auto algorithm = JS_ValueToAtom(ctx, argv[0]);

    // needs an ID?
    bool has_id = (algorithm == S[Algorithm::sm2_sm3]);

    // SM2
    if (algorithm == S[Algorithm::sm2_sm3] ||
        algorithm == S[Algorithm::sm2_sha256]
    ) {
      size_t key_len, id_len;

      // check options
      auto opts = argv[1];
      if (has_id) {
        if (!JS_IsObject(opts)) return JS_ThrowTypeError(ctx, "options expected in argument #2");
      } else {
        if (!JS_IsObject(opts) && !JS_IsString(opts)) return JS_ThrowTypeError(ctx, "key expected in argument #2");
      }

      // check options.key and options.id
      JSValue key, id;
      if (JS_IsObject(opts)) {
        js::Symbols<Option> S(ctx);
        key = JS_GetProperty(ctx, opts, S[Option::key]);
        if (!JS_IsString(key)) return JS_ThrowTypeError(ctx, "options.key expected in argument #2");
        if (has_id) {
          id = JS_GetProperty(ctx, opts, S[Option::id]);
          if (!JS_IsString(id)) return JS_ThrowTypeError(ctx, "options.id expected in argument #2");
        }
      } else {
        key = opts;
      }

      // read pkey
      auto key_str = JS_ToCStringLen(ctx, &key_len, key);
      auto bio = BIO_new_mem_buf(key_str, key_len);
      auto pkey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
      BIO_free(bio);
      JS_FreeCString(ctx, key_str);
      if (!pkey) return JS_ThrowTypeError(ctx, "error reading private key in PEM format");
      EVP_PKEY_set_alias_type(pkey, EVP_PKEY_SM2);

      // create pkey context
      auto pctx = EVP_PKEY_CTX_new(pkey, nullptr);
      if (has_id) {
        auto id_str = JS_ToCStringLen(ctx, &id_len, id);
        EVP_PKEY_CTX_set1_id(pctx, id_str, id_len);
        JS_FreeCString(ctx, id_str);
      }

      // create md context
      auto mctx = EVP_MD_CTX_new();
      EVP_MD_CTX_set_pkey_ctx(mctx, pctx);
      const EVP_MD *md = nullptr;
      if (algorithm == S[Algorithm::sm2_sm3]) md = EVP_sm3();
      else if (algorithm == S[Algorithm::sm2_sha256]) md = EVP_sha256();
      EVP_DigestSignInit(mctx, nullptr, md, nullptr, pkey);

      // create wrapper
      auto sign = new Sign;
      sign->pkey = pkey;
      sign->pctx = pctx;
      sign->mctx = mctx;
      return make(ctx, sign);

    } else {
      return JS_ThrowTypeError(ctx, "unknown algorithm");
    }
  }

  auto Sign::update(JSContext *ctx, JSValueConst this_obj, int argc, JSValueConst *argv) -> JSValue {
    auto p = js::get_cpp_obj<Sign>(this_obj);
    if (!p) return js::throw_invalid_this_type(ctx);
    auto msg = argv[0];
    if (JS_IsString(msg)) {
      size_t len;
      auto str = JS_ToCStringLen(ctx, &len, msg);
      EVP_DigestSignUpdate(p->mctx, str, len);
      return JS_UNDEFINED;
    } else if (auto buf = js::Buffer::get(msg)) {
      auto mctx = p->mctx;
      buf->data.to_chunks([=](const uint8_t *buf, int len) {
        EVP_DigestSignUpdate(mctx, buf, len);
      });
      return JS_UNDEFINED;
    } else {
      return js::throw_invalid_argument_type(ctx);
    }
  }

  auto Sign::final(JSContext *ctx, JSValueConst this_obj, int argc, JSValueConst *argv) -> JSValue {
    auto p = js::get_cpp_obj<Sign>(this_obj);
    if (!p) return js::throw_invalid_this_type(ctx);
    size_t len;
    if (!EVP_DigestSignFinal(p->mctx, nullptr, &len)) return JS_ThrowInternalError(ctx, "cannot finalize");
    unsigned char buf[len];
    if (!EVP_DigestSignFinal(p->mctx, buf, &len)) return JS_ThrowInternalError(ctx, "cannot finalize");
    return js::Buffer::make(ctx, new js::Buffer(buf, len));
  }

  //
  // Verify
  //

  void Verify::define(JSContext *ctx) {
    js::Symbols<Algorithm> s1(ctx);
    js::Symbols<Option> s2(ctx);
    s1.DEFINE_SYMBOL_NAME(Algorithm, sm2_sm3, "sm2-sm3");
    s1.DEFINE_SYMBOL_NAME(Algorithm, sm2_sha256, "sm2-sha256");
    s2.DEFINE_SYMBOL(Option, key);
    s2.DEFINE_SYMBOL(Option, id);
    define_class(ctx);
    define_ctor(ctx, construct, 2, "crypto");
    DEFINE_FUNC(ctx, update, 1);
    DEFINE_FUNC(ctx, final, 1);
  }

  Verify::Verify() {
  }

  Verify::~Verify() {
    EVP_MD_CTX_free(mctx);
    EVP_PKEY_CTX_free(pctx);
    EVP_PKEY_free(pkey);
  }

  auto Verify::construct(JSContext *ctx, JSValueConst this_obj, int argc, JSValueConst *argv) -> JSValue {
    js::Symbols<Algorithm> S(ctx);
    auto algorithm = JS_ValueToAtom(ctx, argv[0]);

    // needs an ID?
    bool has_id = (algorithm == S[Algorithm::sm2_sm3]);

    // SM2
    if (algorithm == S[Algorithm::sm2_sm3] ||
        algorithm == S[Algorithm::sm2_sha256]
    ) {
      size_t key_len, id_len;

      // check options
      auto opts = argv[1];
      if (has_id) {
        if (!JS_IsObject(opts)) return JS_ThrowTypeError(ctx, "options expected in argument #2");
      } else {
        if (!JS_IsObject(opts) && !JS_IsString(opts)) return JS_ThrowTypeError(ctx, "key expected in argument #2");
      }

      // check options.key and options.id
      JSValue key, id;
      if (JS_IsObject(opts)) {
        js::Symbols<Option> S(ctx);
        key = JS_GetProperty(ctx, opts, S[Option::key]);
        if (!JS_IsString(key)) return JS_ThrowTypeError(ctx, "options.key expected in argument #2");
        if (has_id) {
          id = JS_GetProperty(ctx, opts, S[Option::id]);
          if (!JS_IsString(id)) return JS_ThrowTypeError(ctx, "options.id expected in argument #2");
        }
      } else {
        key = opts;
      }

      // read pkey
      auto key_str = JS_ToCStringLen(ctx, &key_len, key);
      auto bio = BIO_new_mem_buf(key_str, key_len);
      auto pkey = PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr);
      BIO_free(bio);
      JS_FreeCString(ctx, key_str);
      if (!pkey) return JS_ThrowTypeError(ctx, "error reading public key in PEM format");
      EVP_PKEY_set_alias_type(pkey, EVP_PKEY_SM2);

      // create pkey context
      auto pctx = EVP_PKEY_CTX_new(pkey, nullptr);
      if (has_id) {
        auto id_str = JS_ToCStringLen(ctx, &id_len, id);
        EVP_PKEY_CTX_set1_id(pctx, id_str, id_len);
        JS_FreeCString(ctx, id_str);
      }

      // create md context
      auto mctx = EVP_MD_CTX_new();
      EVP_MD_CTX_set_pkey_ctx(mctx, pctx);
      const EVP_MD *md = nullptr;
      if (algorithm == S[Algorithm::sm2_sm3]) md = EVP_sm3();
      else if (algorithm == S[Algorithm::sm2_sha256]) md = EVP_sha256();
      EVP_DigestVerifyInit(mctx, nullptr, md, nullptr, pkey);

      // create wrapper
      auto verify = new Verify;
      verify->pkey = pkey;
      verify->pctx = pctx;
      verify->mctx = mctx;
      return make(ctx, verify);

    } else {
      return JS_ThrowTypeError(ctx, "unknown algorithm");
    }
  }

  auto Verify::update(JSContext *ctx, JSValueConst this_obj, int argc, JSValueConst *argv) -> JSValue {
    auto p = js::get_cpp_obj<Verify>(this_obj);
    if (!p) return js::throw_invalid_this_type(ctx);
    auto msg = argv[0];
    if (JS_IsString(msg)) {
      size_t len;
      auto str = JS_ToCStringLen(ctx, &len, msg);
      EVP_DigestVerifyUpdate(p->mctx, str, len);
      JS_FreeCString(ctx, str);
      return JS_UNDEFINED;
    } else if (auto buf = js::Buffer::get(msg)) {
      auto mctx = p->mctx;
      buf->data.to_chunks([=](const uint8_t *buf, int len) {
        EVP_DigestVerifyUpdate(mctx, buf, len);
      });
      return JS_UNDEFINED;
    } else {
      return js::throw_invalid_argument_type(ctx);
    }
  }

  auto Verify::final(JSContext *ctx, JSValueConst this_obj, int argc, JSValueConst *argv) -> JSValue {
    auto p = js::get_cpp_obj<Verify>(this_obj);
    if (!p) return js::throw_invalid_this_type(ctx);
    auto sig = argv[0];
    if (JS_IsString(sig)) {
      size_t len;
      auto str = JS_ToCStringLen(ctx, &len, sig);
      auto ret = EVP_DigestVerifyFinal(p->mctx, (unsigned char*)str, len);
      JS_FreeCString(ctx, str);
      return ret ? JS_TRUE : JS_FALSE;
    } else if (auto buf = js::Buffer::get(sig)) {
      auto str = buf->data.to_string();
      auto ret = EVP_DigestVerifyFinal(p->mctx, (unsigned char*)str.c_str(), str.length());
      return ret == 1 ? JS_TRUE : JS_FALSE;
    } else {
      return js::throw_invalid_argument_type(ctx);
    }
  }

  //
  // Cipher
  //

  void Cipher::define(JSContext *ctx) {
    js::Symbols<Algorithm> s(ctx);
    s.DEFINE_SYMBOL_NAME(Algorithm, sm4_cbc, "sm4-cbc");
    s.DEFINE_SYMBOL_NAME(Algorithm, sm4_ecb, "sm4-ecb");
    s.DEFINE_SYMBOL_NAME(Algorithm, sm4_cfb, "sm4-cfb");
    s.DEFINE_SYMBOL_NAME(Algorithm, sm4_cfb128, "sm4-cfb128");
    s.DEFINE_SYMBOL_NAME(Algorithm, sm4_ofb, "sm4-ofb");
    s.DEFINE_SYMBOL_NAME(Algorithm, sm4_ctr, "sm4-ctr");
    define_class(ctx);
    define_ctor(ctx, construct, 2, "crypto");
    DEFINE_FUNC(ctx, update, 1);
    DEFINE_FUNC(ctx, final, 0);
  }

  Cipher::Cipher() {
  }

  Cipher::~Cipher() {
    EVP_CIPHER_CTX_free(cctx);
  }

  auto Cipher::construct(JSContext *ctx, JSValueConst this_obj, int argc, JSValueConst *argv) -> JSValue {
    js::Symbols<Algorithm> S(ctx);
    auto algorithm = JS_ValueToAtom(ctx, argv[0]);

    auto init_sm4 = [&](const EVP_CIPHER *alg) -> JSValue {
      auto key = js::get_as_string(ctx, argv[1]);
      auto iv = js::get_as_string(ctx, argv[2]);
      if (key.length() != 16) return JS_ThrowRangeError(ctx, "expected key length of 128 bits");
      if (iv.length() != 16) return JS_ThrowRangeError(ctx, "expected IV length of 128 bits");

      auto cctx = EVP_CIPHER_CTX_new();
      if (!EVP_EncryptInit_ex(cctx, alg, nullptr, (const unsigned char*)key.c_str(), (const unsigned char*)iv.c_str())) {
        return JS_ThrowTypeError(ctx, "cipher initialization failed");
      }

      auto cipher = new Cipher;
      cipher->cctx = cctx;
      return make(ctx, cipher);
    };

    if (algorithm == S[Algorithm::sm4_cbc]) return init_sm4(EVP_sm4_cbc());
    else if (algorithm == S[Algorithm::sm4_ecb]) return init_sm4(EVP_sm4_ecb());
    else if (algorithm == S[Algorithm::sm4_cfb]) return init_sm4(EVP_sm4_cfb());
    else if (algorithm == S[Algorithm::sm4_cfb128]) return init_sm4(EVP_sm4_cfb128());
    else if (algorithm == S[Algorithm::sm4_ofb]) return init_sm4(EVP_sm4_ofb());
    else if (algorithm == S[Algorithm::sm4_ctr]) return init_sm4(EVP_sm4_ctr());
    else return JS_ThrowTypeError(ctx, "unknown algorithm");
  }

  auto Cipher::update(JSContext *ctx, JSValueConst this_obj, int argc, JSValueConst *argv) -> JSValue {
    auto p = js::get_cpp_obj<Cipher>(this_obj);
    if (!p) return js::throw_invalid_this_type(ctx);
    auto block_size = EVP_CIPHER_CTX_block_size(p->cctx);
    js::Buffer *ret = nullptr;
    if (auto buf = js::Buffer::get(argv[0])) {
      int i = 0;
      uint8_t out[buf->data.size() + block_size];
      buf->data.to_chunks([&](const uint8_t *buf, int len) {
        int n = 0;
        EVP_EncryptUpdate(p->cctx, out + i, &n, buf, len);
        i += n;
      });
      ret = new js::Buffer(out, i);
    } else if (JS_IsString(argv[0])) {
      js::CStr str(ctx, argv[0]);
      uint8_t out[str.len + block_size];
      int len = 0;
      EVP_EncryptUpdate(p->cctx, out, &len, (const unsigned char*)str.ptr, str.len);
      ret = new js::Buffer(out, len);
    }
    return js::Buffer::make(ctx, ret);
  }

  auto Cipher::final(JSContext *ctx, JSValueConst this_obj, int argc, JSValueConst *argv) -> JSValue {
    auto p = js::get_cpp_obj<Cipher>(this_obj);
    if (!p) return js::throw_invalid_this_type(ctx);
    auto block_size = EVP_CIPHER_CTX_block_size(p->cctx);
    uint8_t out[block_size];
    int len = 0;
    EVP_EncryptFinal(p->cctx, out, &len);
    return js::Buffer::make(ctx, new js::Buffer(out, len));
  }

  //
  // Decipher
  //

  void Decipher::define(JSContext *ctx) {
    js::Symbols<Algorithm> s(ctx);
    s.DEFINE_SYMBOL_NAME(Algorithm, sm4_cbc, "sm4-cbc");
    s.DEFINE_SYMBOL_NAME(Algorithm, sm4_ecb, "sm4-ecb");
    s.DEFINE_SYMBOL_NAME(Algorithm, sm4_cfb, "sm4-cfb");
    s.DEFINE_SYMBOL_NAME(Algorithm, sm4_cfb128, "sm4-cfb128");
    s.DEFINE_SYMBOL_NAME(Algorithm, sm4_ofb, "sm4-ofb");
    s.DEFINE_SYMBOL_NAME(Algorithm, sm4_ctr, "sm4-ctr");
    define_class(ctx);
    define_ctor(ctx, construct, 2, "crypto");
    DEFINE_FUNC(ctx, update, 1);
    DEFINE_FUNC(ctx, final, 0);
  }

  Decipher::Decipher() {
  }

  Decipher::~Decipher() {
    EVP_CIPHER_CTX_free(cctx);
  }

  auto Decipher::construct(JSContext *ctx, JSValueConst this_obj, int argc, JSValueConst *argv) -> JSValue {
    js::Symbols<Algorithm> S(ctx);
    auto algorithm = JS_ValueToAtom(ctx, argv[0]);

    auto init_sm4 = [&](const EVP_CIPHER *alg) -> JSValue {
      auto key = js::get_as_string(ctx, argv[1]);
      auto iv = js::get_as_string(ctx, argv[2]);
      if (key.length() != 16) return JS_ThrowRangeError(ctx, "expected key length of 128 bits");
      if (iv.length() != 16) return JS_ThrowRangeError(ctx, "expected IV length of 128 bits");

      auto cctx = EVP_CIPHER_CTX_new();
      if (!EVP_DecryptInit_ex(cctx, alg, nullptr, (const unsigned char*)key.c_str(), (const unsigned char*)iv.c_str())) {
        return JS_ThrowTypeError(ctx, "cipher initialization failed");
      }

      auto decipher = new Decipher;
      decipher->cctx = cctx;
      return make(ctx, decipher);
    };

    if (algorithm == S[Algorithm::sm4_cbc]) return init_sm4(EVP_sm4_cbc());
    else if (algorithm == S[Algorithm::sm4_ecb]) return init_sm4(EVP_sm4_ecb());
    else if (algorithm == S[Algorithm::sm4_cfb]) return init_sm4(EVP_sm4_cfb());
    else if (algorithm == S[Algorithm::sm4_cfb128]) return init_sm4(EVP_sm4_cfb128());
    else if (algorithm == S[Algorithm::sm4_ofb]) return init_sm4(EVP_sm4_ofb());
    else if (algorithm == S[Algorithm::sm4_ctr]) return init_sm4(EVP_sm4_ctr());
    else return JS_ThrowTypeError(ctx, "unknown algorithm");
  }

  auto Decipher::update(JSContext *ctx, JSValueConst this_obj, int argc, JSValueConst *argv) -> JSValue {
    auto p = js::get_cpp_obj<Decipher>(this_obj);
    if (!p) return js::throw_invalid_this_type(ctx);
    auto block_size = EVP_CIPHER_CTX_block_size(p->cctx);
    js::Buffer *ret = nullptr;
    if (auto buf = js::Buffer::get(argv[0])) {
      int i = 0;
      uint8_t out[buf->data.size() + block_size];
      buf->data.to_chunks([&](const uint8_t *buf, int len) {
        int n = 0;
        EVP_DecryptUpdate(p->cctx, out + i, &n, buf, len);
        i += n;
      });
      ret = new js::Buffer(out, i);
    } else if (JS_IsString(argv[0])) {
      js::CStr str(ctx, argv[0]);
      uint8_t out[str.len + block_size];
      int len = 0;
      EVP_DecryptUpdate(p->cctx, out, &len, (const unsigned char*)str.ptr, str.len);
      ret = new js::Buffer(out, len);
    }
    return js::Buffer::make(ctx, ret);
  }

  auto Decipher::final(JSContext *ctx, JSValueConst this_obj, int argc, JSValueConst *argv) -> JSValue {
    auto p = js::get_cpp_obj<Decipher>(this_obj);
    if (!p) return js::throw_invalid_this_type(ctx);
    auto block_size = EVP_CIPHER_CTX_block_size(p->cctx);
    uint8_t out[block_size];
    int len = 0;
    EVP_DecryptFinal(p->cctx, out, &len);
    return js::Buffer::make(ctx, new js::Buffer(out, len));
  }

} // namespace crypto

NS_END