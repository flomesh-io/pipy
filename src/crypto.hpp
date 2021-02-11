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

#include "js.hpp"
#include "pool.hpp"

#include <openssl/evp.h>

NS_BEGIN

namespace crypto {

  //
  // Sign
  //

  class Sign : public js::Class<Sign>, public Pooled<Sign> {
  public:
    static void define(JSContext *ctx);

    enum class Algorithm {
      sm2_sm3,
      sm2_sha256,
      __MAX__,
    };

    enum class Option {
      key,
      id,
      __MAX__,
    };

    Sign();
    ~Sign();

    EVP_PKEY* pkey = nullptr;
    EVP_PKEY_CTX* pctx = nullptr;
    EVP_MD_CTX* mctx = nullptr;

  private:
    static auto construct(JSContext *ctx, JSValueConst this_obj, int argc, JSValueConst *argv) -> JSValue;
    static auto update(JSContext *ctx, JSValueConst this_obj, int argc, JSValueConst *argv) -> JSValue;
    static auto final(JSContext *ctx, JSValueConst this_obj, int argc, JSValueConst *argv) -> JSValue;
  };

  //
  // Verify
  //

  class Verify : public js::Class<Verify>, public Pooled<Verify> {
  public:
    static void define(JSContext *ctx);

    enum class Algorithm {
      sm2_sm3,
      sm2_sha256,
      __MAX__,
    };

    enum class Option {
      key,
      id,
      __MAX__,
    };

    Verify();
    ~Verify();

    EVP_PKEY* pkey = nullptr;
    EVP_PKEY_CTX* pctx = nullptr;
    EVP_MD_CTX* mctx = nullptr;

  private:
    static auto construct(JSContext *ctx, JSValueConst this_obj, int argc, JSValueConst *argv) -> JSValue;
    static auto update(JSContext *ctx, JSValueConst this_obj, int argc, JSValueConst *argv) -> JSValue;
    static auto final(JSContext *ctx, JSValueConst this_obj, int argc, JSValueConst *argv) -> JSValue;
  };

  //
  // Cipher
  //

  class Cipher : public js::Class<Cipher>, public Pooled<Cipher> {
  public:
    static void define(JSContext *ctx);

    enum class Algorithm {
      sm4_cbc,
      sm4_ecb,
      sm4_cfb,
      sm4_cfb128,
      sm4_ofb,
      sm4_ctr,
      __MAX__,
    };

    Cipher();
    ~Cipher();

    EVP_CIPHER_CTX* cctx = nullptr;

  private:
    static auto construct(JSContext *ctx, JSValueConst this_obj, int argc, JSValueConst *argv) -> JSValue;
    static auto update(JSContext *ctx, JSValueConst this_obj, int argc, JSValueConst *argv) -> JSValue;
    static auto final(JSContext *ctx, JSValueConst this_obj, int argc, JSValueConst *argv) -> JSValue;
  };

  //
  // Decipher
  //

  class Decipher : public js::Class<Decipher>, public Pooled<Decipher> {
  public:
    static void define(JSContext *ctx);

    enum class Algorithm {
      sm4_cbc,
      sm4_ecb,
      sm4_cfb,
      sm4_cfb128,
      sm4_ofb,
      sm4_ctr,
      __MAX__,
    };

    Decipher();
    ~Decipher();

    EVP_CIPHER_CTX* cctx = nullptr;

  private:
    static auto construct(JSContext *ctx, JSValueConst this_obj, int argc, JSValueConst *argv) -> JSValue;
    static auto update(JSContext *ctx, JSValueConst this_obj, int argc, JSValueConst *argv) -> JSValue;
    static auto final(JSContext *ctx, JSValueConst this_obj, int argc, JSValueConst *argv) -> JSValue;
  };

} // namespace crypto

NS_END

#endif // CRYPTO_HPP