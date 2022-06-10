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

#include "detect-protocol.hpp"
#include "data.hpp"

#include <set>

namespace pipy {

static pjs::ConstStr STR_HTTP("HTTP");
static pjs::ConstStr STR_TLS("TLS");

//
// HTTPDetector
//

class HTTPDetector :
  public pjs::Pooled<HTTPDetector>,
  public ProtocolDetector::Detector
{
  virtual auto feed(const char *data, size_t size) -> pjs::Str* override {
    auto &p = m_read_length;
    auto &s = m_read_buffer;
    for (size_t i = 0; i < size; i++) {
      auto c = data[i];
      if (m_read_protocol) {
        s[p++] = c;
        if (p == 7) {
          if (std::strncmp(s, "HTTP/1.", 7)) {
            return pjs::Str::empty;
          } else {
            break;
          }
        }
      } else if (m_seen_method) {
        if (c == ' ') {
          p = 0;
          m_read_protocol = true;
        }
      } else {
        if (c == ' ') {
          std::string method(s, p);
          if (s_valid_methods.count(method)) {
            m_seen_method = true;
          } else {
            return pjs::Str::empty;
          }
        } else if (p < sizeof(s)) {
          s[p++] = c;
        } else {
          return pjs::Str::empty;
        }
      }
    }
    if (m_seen_method) return STR_HTTP;
    return nullptr;
  }

  char m_read_buffer[8];
  size_t m_read_length = 0;
  bool m_seen_method = false;
  bool m_read_protocol = false;

  static std::set<std::string> s_valid_methods;
};

std::set<std::string> HTTPDetector::s_valid_methods({
  "GET",
  "HEAD",
  "POST",
  "PUT",
  "DELETE",
  "CONNECT",
  "OPTIONS",
  "TRACE",
  "PATCH",
});

//
// TLSDetector
//

class TLSDetector :
  public pjs::Pooled<TLSDetector>,
  public ProtocolDetector::Detector
{
  uint8_t m_read_buffer[11];
  size_t m_read_length = 0;

  virtual auto feed(const char *data, size_t size) -> pjs::Str* override {
    auto &buf = m_read_buffer;
    for (size_t i = 0; i < size; i++) {
      auto c = data[i];
      buf[m_read_length++] = c;
      switch (m_read_length) {
      case 1:
        if (buf[0] != 22) {
          return pjs::Str::empty;
        }
        break;
      case 2:
        if (buf[1] != 3) {
          return pjs::Str::empty;
        }
        break;
      case 3:
        if (buf[2] > 4) {
          return pjs::Str::empty;
        }
        break;
      case 6:
        if (buf[5] != 1) {
          return pjs::Str::empty;
        }
        break;
      case 9:
        if (
          (((uint32_t)buf[6] << 16) + ((uint32_t)buf[7] << 8) + buf[8]) >
          (((uint16_t)buf[3] << 8) + buf[4]) + 4
        ) {
          return pjs::Str::empty;
        }
        break;
      case 11:
        return STR_TLS;
      }
    }
    return nullptr;
  }
};

//
// ProtocolDetector
//

ProtocolDetector::ProtocolDetector(pjs::Function *callback)
  : m_callback(callback)
{
}

ProtocolDetector::ProtocolDetector(const ProtocolDetector &r)
  : Filter(r)
  , m_callback(r.m_callback)
{
}

ProtocolDetector::~ProtocolDetector()
{
}

auto ProtocolDetector::clone() -> Filter* {
  return new ProtocolDetector(*this);
}

void ProtocolDetector::reset() {
  Filter::reset();
  for (int i = 0; i < m_num_detectors; i++) {
    delete m_detectors[i];
    m_detectors[i] = nullptr;
  }
  m_negatives = 0;
  m_result = nullptr;
  m_num_detectors = 2;
  m_detectors[0] = new HTTPDetector;
  m_detectors[1] = new TLSDetector;
}

void ProtocolDetector::dump(Dump &d) {
  Filter::dump(d);
  d.name = "detectProtocol";
}

void ProtocolDetector::process(Event *evt) {
  if (!m_result) {
    if (auto data = evt->as<Data>()) {
      for (const auto c : data->chunks()) {
        auto data = std::get<0>(c);
        auto size = std::get<1>(c);
        for (int i = 0; i < m_num_detectors; i++) {
          if (auto *detector = m_detectors[i]) {
            if (auto ret = detector->feed(data, size)) {
              if (ret == pjs::Str::empty) {
                delete detector;
                m_detectors[i] = nullptr;
                if (++m_negatives == m_num_detectors) {
                  m_result = ret;
                  break;
                }
              } else {
                m_result = ret;
                break;
              }
            }
          }
        }
        if (m_result) break;
      }

      if (m_result) {
        pjs::Value arg, ret;
        arg.set(m_result);
        callback(m_callback, 1, &arg, ret);
      }
    }
  }

  output(evt);
}

} // namespace pipy
