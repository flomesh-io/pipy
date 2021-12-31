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

namespace pipy {

static pjs::ConstStr STR_TLS("TLS");

//
// TLSDetector
//

class TLSDetector :
  public pjs::Pooled<TLSDetector>,
  public ProtocolDetector::Detector
{
  uint8_t m_read_buffer[11];
  size_t m_read_length = 0;

  virtual auto feed(int c) -> pjs::Str* override {
    auto &buf = m_read_buffer;
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
  m_num_detectors = 1;
  m_detectors[0] = new TLSDetector;
}

void ProtocolDetector::dump(std::ostream &out) {
  out << "detectProtocol";
}

void ProtocolDetector::process(Event *evt) {
  if (!m_result) {
    if (auto data = evt->as<Data>()) {
      data->scan(
        [this](int c) {
          for (int i = 0; i < m_num_detectors; i++) {
            if (auto *detector = m_detectors[i]) {
              if (auto ret = detector->feed(c)) {
                if (ret == pjs::Str::empty) {
                  delete detector;
                  m_detectors[i] = nullptr;
                  if (++m_negatives == m_num_detectors) {
                    m_result = ret;
                    return false;
                  }
                } else {
                  m_result = ret;
                  return false;
                }
              }
            }
          }
          return true;
        }
      );

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
