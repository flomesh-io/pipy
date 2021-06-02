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

#include "decompress-body.hpp"
#include "data.hpp"
#include "pjs/pjs.hpp"
#include "logging.hpp"

#define ZLIB_CONST
#include <zlib.h>

namespace pipy {

//
// Inflate
//

class Inflate : public pjs::Pooled<Inflate>, public DecompressBody::Decompressor {
public:
  Inflate(const Event::Receiver &out)
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
  const Event::Receiver& m_out;
  z_stream m_zs;
  bool m_done = false;

  ~Inflate() {
    inflateEnd(&m_zs);
  }

  virtual bool process(const Data *data) override {
    if (m_done) return true;
    unsigned char buf[DATA_CHUNK_SIZE];
    pjs::Ref<Data> output_data(Data::make());
    for (const auto &chk : data->chunks()) {
      m_zs.next_in = (const unsigned char *)std::get<0>(chk);
      m_zs.avail_in = std::get<1>(chk);
      do {
        m_zs.next_out = buf;
        m_zs.avail_out = sizeof(buf);
        auto ret = inflate(&m_zs, Z_NO_FLUSH);
        if (auto size = sizeof(buf) - m_zs.avail_out) {
          output_data->push(buf, size);
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
// DecompressBody
//

DecompressBody::DecompressBody()
  : DecompressBody(Algorithm::INFLATE)
{
}

DecompressBody::DecompressBody(Algorithm algorithm)
  : m_algorithm(algorithm)
{
}

DecompressBody::DecompressBody(const DecompressBody &r)
  : DecompressBody(r.m_algorithm)
{
}

DecompressBody::~DecompressBody()
{
}

auto DecompressBody::help() -> std::list<std::string> {
  return {
    "decompressMessageBody(algorithm)",
    "Decompresses the data in message bodies",
    "algorithm = <string> Currently can be 'inflate' only",
  };
}

void DecompressBody::dump(std::ostream &out) {
  out << "decompressMessageBody";
}

auto DecompressBody::clone() -> Filter* {
  return new DecompressBody(*this);
}

void DecompressBody::reset()
{
  if (m_decompressor) {
    m_decompressor->end();
    m_decompressor = nullptr;
  }
  m_session_end = false;
}

void DecompressBody::process(Context *ctx, Event *inp) {
  if (m_session_end) return;

  if (auto *data = inp->as<Data>()) {
    if (m_decompressor) {
      if (!m_decompressor->process(data)) {
        Log::warn("[decompress] decompression error");
        m_decompressor->end();
        m_decompressor = nullptr;
      }
    }
    return;
  }

  if (inp->is<MessageStart>()) {
    if (!m_decompressor) {
      switch (m_algorithm) {
        case Algorithm::INFLATE:
          m_decompressor = new Inflate(out());
          break;
      }
    }

  } else if (inp->is<MessageEnd>()) {
    if (m_decompressor) {
      m_decompressor->end();
      m_decompressor = nullptr;
    }

  } else if (inp->is<SessionEnd>()) {
    m_session_end = true;
  }

  output(inp);
}

} // namespace pipy

namespace pjs {

using namespace pipy;

template<> void EnumDef<DecompressBody::Algorithm>::init() {
  define(DecompressBody::Algorithm::INFLATE, "inflate");
}

} // namespace pjs
