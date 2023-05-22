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

#include "compressor.hpp"
#include "data.hpp"
#include "pjs/pjs.hpp"

#define ZLIB_CONST
#include <zlib.h>

#include <brotli/decode.h>

namespace pipy {

thread_local static Data::Producer s_dp_inflate("inflate");
thread_local static Data::Producer s_dp_brotli("brotli");

//
// Inflate
//

class Inflate : public pjs::Pooled<Inflate>, public Decompressor {
public:
  Inflate(const std::function<void(Data&)> &out, bool gzip)
    : m_out(out)
  {
    m_zs.zalloc = Z_NULL;
    m_zs.zfree = Z_NULL;
    m_zs.opaque = Z_NULL;
    m_zs.next_in = Z_NULL;
    m_zs.avail_in = 0;
    inflateInit2(&m_zs, gzip ? 16 + MAX_WBITS : MAX_WBITS);
  }

private:
  const std::function<void(Data&)> m_out;
  z_stream m_zs;
  bool m_done = false;

  ~Inflate() {
    inflateEnd(&m_zs);
  }

  virtual bool input(const Data &data) override {
    if (m_done) return true;

    unsigned char buf[DATA_CHUNK_SIZE];
    Data output;
    Data::Builder db(output, &s_dp);

    for (const auto chk : data.chunks()) {
      m_zs.next_in = (const unsigned char *)std::get<0>(chk);
      m_zs.avail_in = std::get<1>(chk);
      do {
        m_zs.next_out = buf;
        m_zs.avail_out = sizeof(buf);
        auto ret = ::inflate(&m_zs, Z_NO_FLUSH);
        if (auto size = sizeof(buf) - m_zs.avail_out) {
          db.push(buf, size);
        }
        if (ret == Z_STREAM_END) { m_done = true; break; }
        if (ret != Z_OK) return false;
      } while (m_zs.avail_out == 0);
      if (m_done) break;
    }

    db.flush();
    m_out(output);
    return true;
  }

  virtual bool finalize() override {
    delete this;
    return true;
  }

  thread_local static Data::Producer s_dp;
};

thread_local Data::Producer Inflate::s_dp("Decompress (inflate)");

//
// BrotliDecoder
//

class BrotliDecoder : public pjs::Pooled<BrotliDecoder>, public Decompressor {
public:
  BrotliDecoder(const std::function<void(Data&)> &out)
    : m_out(out)
    , m_ds(BrotliDecoderCreateInstance(NULL, NULL, NULL))
  {
    BrotliDecoderSetParameter(m_ds, BROTLI_DECODER_PARAM_LARGE_WINDOW, 1u);
  }

private:
  const std::function<void(Data&)> m_out;
  BrotliDecoderState* m_ds;
  bool m_done = false;

  ~BrotliDecoder() {
    BrotliDecoderDestroyInstance(m_ds);
  }

  virtual bool input(const Data &data) override {
    if (m_done) return true;

    uint8_t buf[DATA_CHUNK_SIZE];
    Data output;
    Data::Builder db(output, &s_dp);

    const unsigned char *next_in = nullptr;
    uint8_t *next_out = buf;
    size_t avail_in = 0, avail_out = DATA_CHUNK_SIZE;
    BrotliDecoderResult result;

    for (const auto chk : data.chunks()) {
      next_in = (const unsigned char *)std::get<0>(chk);
      avail_in = std::get<1>(chk);

      for(;;) {
        result = BrotliDecoderDecompressStream(m_ds, &avail_in, &next_in, &avail_out, &next_out, 0);
        switch (result) {
          case BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT:
            if (auto size = (size_t)(next_out - buf)) {
              db.push(buf, size);
            }
            avail_out = DATA_CHUNK_SIZE;
            next_out = buf;
            break;
          case BROTLI_DECODER_RESULT_SUCCESS:
            if (auto size = (size_t)(next_out - buf)) {
              db.push(buf, size);
            }
            avail_out = 0;
            if (avail_in != 0) return false;
            m_done = true;
            break;
          case BROTLI_DECODER_RESULT_ERROR:
            return false;
          case BROTLI_DECODER_RESULT_NEEDS_MORE_INPUT:
            break;
        }
        if (result == BROTLI_DECODER_RESULT_NEEDS_MORE_INPUT ||
            result == BROTLI_DECODER_RESULT_SUCCESS) break;
      }
    }

    db.flush();
    m_out(output);
    return true;
  }

  virtual bool finalize() override {
    delete this;
    return true;
  }

  thread_local static Data::Producer s_dp;
};

thread_local Data::Producer BrotliDecoder::s_dp("Decompress (brotli)");

//
// Deflate
//

class Deflate : public pjs::Pooled<Deflate>, public Compressor {
public:
  enum class Method {
    deflate,
    gzip,
  };

  Deflate(const Output &out, bool gzip)
    : m_out(out)
  {
    m_zs.zalloc = Z_NULL;
    m_zs.zfree = Z_NULL;
    m_zs.opaque = Z_NULL;
    m_zs.next_in = Z_NULL;
    m_zs.avail_out = 0;

    deflateInit2(
      &m_zs,
      Z_DEFAULT_COMPRESSION,
      Z_DEFLATED,
      gzip ? 16 + MAX_WBITS : MAX_WBITS,
      8,
      Z_DEFAULT_STRATEGY
    );
  }

private:
  Output m_out;
  z_stream m_zs;

  ~Deflate(){
    deflateEnd(&m_zs);
  }

  virtual bool input(const Data &data, bool flush) override {
    Data output;
    Data::Builder db(output, &s_dp);

    for (const auto chk : data.chunks()) {
      auto buf = std::get<0>(chk);
      auto len = std::get<1>(chk);
      if (!deflate(buf, len, flush, db)) return false;
    }

    db.flush();
    m_out(output);
    return true;
  }

  virtual bool flush() override {
    Data output;
    Data::Builder db(output, &s_dp);
    if (!deflate(nullptr, 0, true, db)) return false;
    db.flush();
    m_out(output);
    return true;
  }

  virtual bool finalize() override {
    delete this;
    return true;
  }

  bool deflate(const char *data, size_t size, bool flush, Data::Builder &db) {
    unsigned char buf[DATA_CHUNK_SIZE];
    m_zs.next_in = (const Bytef *)data;
    m_zs.avail_in = size;
    do {
      m_zs.next_out = buf;
      m_zs.avail_out = sizeof(buf);
      auto ret = ::deflate(&m_zs, flush ? Z_FINISH : Z_NO_FLUSH);
      if (ret == Z_STREAM_ERROR) return false;
      if (auto size = sizeof(buf) - m_zs.avail_out) db.push(buf, size);
    } while (m_zs.avail_out == 0);
    return true;
  }

  thread_local static Data::Producer s_dp;
};

thread_local Data::Producer Deflate::s_dp("Compress (defalte)");

//
// Decompressor
//

Decompressor* Decompressor::inflate(const std::function<void(Data&)> &out) {
  return new Inflate(out, false);
}

Decompressor* Decompressor::gzip(const std::function<void(Data&)> &out) {
  return new Inflate(out, true);
}

Decompressor* Decompressor::brotli(const std::function<void(Data&)> &out) {
  return new BrotliDecoder(out);
}

//
// Compressor
//

Compressor *Compressor::deflate(const Output &out) {
  return new Deflate(out, false);
}

Compressor *Compressor::gzip(const Output &out) {
  return new Deflate(out, true);
}

} // namespace pipy
