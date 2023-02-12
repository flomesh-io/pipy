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
#include "str-map.hpp"

namespace pipy {

thread_local static pjs::ConstStr STR_HTTP("HTTP");
thread_local static pjs::ConstStr STR_HTTP2("HTTP2");
thread_local static pjs::ConstStr STR_TLS("TLS");

//
// HTTPDetector
//

class HTTPDetector :
  public pjs::Pooled<HTTPDetector>,
  public ProtocolDetector::Detector
{
public:
  HTTPDetector()
    : m_method_parser(s_valid_methods)
    , m_version_parser(s_valid_versions) {}

private:
  virtual auto feed(const char *data, size_t size) -> pjs::Str* override {
    for (size_t i = 0; i < size; i++) {
      auto c = data[i];
      switch (m_state) {
        case CHECK_METHOD:
          if (auto found = m_method_parser.parse(c)) {
            if (found == pjs::Str::empty) return found;
            m_state = CHECK_PATH;
          }
          break;
        case CHECK_PATH:
          if (c == ' ') m_state = CHECK_VERSION;
          break;
        case CHECK_VERSION:
          if (auto found = m_version_parser.parse(c)) {
            if (found == pjs::Str::empty) return found;
            return STR_HTTP;
          }
          break;
        default: break;
      }
    }
    return nullptr;
  }

  enum State {
    CHECK_METHOD,
    CHECK_PATH,
    CHECK_VERSION,
  };

  State m_state = CHECK_METHOD;
  StrMap::Parser m_method_parser;
  StrMap::Parser m_version_parser;

  thread_local static const StrMap s_valid_methods;
  thread_local static const StrMap s_valid_versions;
};

thread_local const StrMap HTTPDetector::s_valid_methods({
  "GET ", "HEAD ", "POST ", "PUT ",
  "PATCH ", "DELETE ", "CONNECT ", "OPTIONS ", "TRACE ",
});

thread_local const StrMap HTTPDetector::s_valid_versions({
  "HTTP/1.0\r\n", "HTTP/1.1\r\n",
});

//
// HTTP2Detector
//

class HTTP2Detector :
  public pjs::Pooled<HTTP2Detector>,
  public ProtocolDetector::Detector
{
  virtual auto feed(const char *data, size_t size) -> pjs::Str* override {
    auto n = std::min(size, s_prefix.length() - m_pointer);
    if (std::strncmp(data, s_prefix.c_str() + m_pointer, n)) {
      return pjs::Str::empty;
    }
    m_pointer += n;
    if (m_pointer == s_prefix.length()) return STR_HTTP2;
    return nullptr;
  }

  size_t m_pointer = 0;

  static const std::string s_prefix;
};

const std::string HTTP2Detector::s_prefix("PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n");

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
  m_num_detectors = 3;
  m_detectors[0] = new HTTPDetector;
  m_detectors[1] = new HTTP2Detector;
  m_detectors[2] = new TLSDetector;
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
