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

#include "compress.hpp"
#include "data.hpp"
#include "pjs/pjs.hpp"

#define ZLIB_CONST
#include <zlib.h>

namespace pipy {

//
// Inflate
//

class Inflate : public pjs::Pooled<Inflate>, public Decompressor {
public:
  Inflate(const std::function<void(Data*)> &out)
    : m_out(out)
  {
    m_zs.zalloc = Z_NULL;
    m_zs.zfree = Z_NULL;
    m_zs.opaque = Z_NULL;
    m_zs.next_in = Z_NULL;
    m_zs.avail_in = 0;
    inflateInit2(&m_zs, 16 + MAX_WBITS);
  }

private:
  const std::function<void(Data*)> m_out;
  z_stream m_zs;
  bool m_done = false;

  ~Inflate() {
    inflateEnd(&m_zs);
  }

  virtual bool process(const Data *data) override {
    static Data::Producer s_dp("inflate");

    if (m_done) return true;
    unsigned char buf[DATA_CHUNK_SIZE];
    pjs::Ref<Data> output_data(Data::make());
    for (const auto chk : data->chunks()) {
      m_zs.next_in = (const unsigned char *)std::get<0>(chk);
      m_zs.avail_in = std::get<1>(chk);
      do {
        m_zs.next_out = buf;
        m_zs.avail_out = sizeof(buf);
        auto ret = ::inflate(&m_zs, Z_NO_FLUSH);
        if (auto size = sizeof(buf) - m_zs.avail_out) {
          s_dp.push(output_data, buf, size);
        }
        if (ret == Z_STREAM_END) { m_done = true; break; }
        if (ret != Z_OK) {
          inflateEnd(&m_zs);
          return false;
        }
      } while (m_zs.avail_out == 0);
      if (m_done) break;
    }
    m_out(output_data);
    return true;
  }

  virtual bool end() override {
    delete this;
    return true;
  }
};

//
// Deflate
//

class Deflate : public pjs::Pooled<Deflate>, public Compressor {
public:
  enum class CompressionMethod {
      deflate = MAX_WBITS,
      gzip = 16 + MAX_WBITS,
  };

  Deflate(const std::function<void(Data*)> &in, CompressionMethod method = CompressionMethod::gzip, int level = Z_DEFAULT_COMPRESSION)
    : m_in(in)
    , m_method(method)
  {
    m_zs.zalloc = Z_NULL;
    m_zs.zfree = Z_NULL;
    m_zs.opaque = Z_NULL;
    m_zs.next_in = Z_NULL;
    m_zs.avail_out = 0;
    auto ret = deflateInit2(&m_zs, level, Z_DEFLATED, static_cast<int>(method), 8, Z_DEFAULT_STRATEGY);

    if (ret != Z_OK) {
      throw std::runtime_error("[compress] zlib init failed");
    }
  }

private:
  const std::function<void(Data*)> m_in;
  z_stream m_zs;
  CompressionMethod m_method;

  ~Deflate(){
    deflateEnd(&m_zs);
  }

  virtual bool process(const Data *data) override {
    static Data::Producer s_dp(m_method == CompressionMethod::deflate ? "deflate" : "gzip");

    auto len = data->size();
    unsigned char buf[DATA_CHUNK_SIZE];
    pjs::Ref<Data> output_data(Data::make());

    for (const auto chk : data->chunks()) {
      m_zs.next_in = (const unsigned char*)std::get<0>(chk);
      m_zs.avail_in = std::get<1>(chk);
      len -= m_zs.avail_in;
      auto flush = len ? Z_NO_FLUSH : Z_FINISH;
      do {
        m_zs.avail_out = DATA_CHUNK_SIZE;
        m_zs.next_out = buf;

        auto ret = ::deflate(&m_zs, flush);
        if (ret == Z_STREAM_ERROR) {
          deflateEnd(&m_zs);
          return false;
        }
        if (auto size = DATA_CHUNK_SIZE - m_zs.avail_out) {
          s_dp.push(output_data, buf, size);
        }
      } while (m_zs.avail_out == 0);
    }
    if (m_zs.avail_in != 0) {
      throw std::runtime_error("[compress] not all input used.");
    }
    m_in(output_data);
    return true;
  }

  virtual bool end() override {
    delete this;
    return true;
  }
};
//
// Decompressor
//

Decompressor* Decompressor::inflate(const std::function<void(Data*)> &out) {
  return new Inflate(out);
}


//
// Decompressor
//

Compressor *Compressor::deflate(const std::function<void(Data *)> &in, int compression_level) {
    return new Deflate(in,Deflate::CompressionMethod::deflate, compression_level);
}

Compressor *Compressor::gzip(const std::function<void(Data *)> &in, int compression_level) {
  return new Deflate(in,Deflate::CompressionMethod::gzip, compression_level);
}

Compressor *Compressor::brotli(const std::function<void(Data *)> &in, int compression_level) {
  throw std::runtime_error("Brotli compression not implemented");
}
} // namespace pipy
