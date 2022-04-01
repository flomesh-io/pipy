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
// Decompressor
//

Decompressor* Decompressor::inflate(const std::function<void(Data*)> &out) {
  return new Inflate(out);
}

} // namespace pipy
