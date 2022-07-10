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

#include "mime.hpp"
#include "kmp.hpp"
#include "utils.hpp"
#include "log.hpp"

namespace pipy {
namespace mime {

static std::string s_multipart("multipart/");
static std::string s_boundary("boundary=");
static pjs::ConstStr s_content_type("content-type");

//
// MultipartDecoder
//

MultipartDecoder::MultipartDecoder()
  : m_prop_headers("headers")
{
}

MultipartDecoder::MultipartDecoder(const MultipartDecoder &r)
  : MultipartDecoder()
{
}

MultipartDecoder::~MultipartDecoder()
{
}

void MultipartDecoder::dump(Dump &d) {
  Filter::dump(d);
  d.name = "decodeMultipart";
}

auto MultipartDecoder::clone() -> Filter* {
  return new MultipartDecoder(*this);
}

void MultipartDecoder::reset() {
  Filter::reset();
  delete m_current_multipart;
  m_current_multipart = nullptr;
}

void MultipartDecoder::process(Event *evt) {
  if (auto *start = evt->as<MessageStart>()) {
    if (!m_current_multipart) {
      if (auto *head = start->head()) {
        pjs::Value v;
        m_prop_headers.get(head, v);
        if (v.is_object()) {
          v.o()->get(s_content_type, v);
          if (v.is_string()) {
            m_current_multipart = multipart_start(v.s()->str());
          }
        }
      }
    }
    if (!m_current_multipart) Filter::output(evt);

  } else if (auto *data = evt->as<Data>()) {
    if (m_current_multipart) {
      m_current_multipart->parse(*data);
    } else {
      Filter::output(evt);
    }

  } else if (evt->is<MessageEnd>() || evt->is<StreamEnd>()) {
    if (m_current_multipart) {
      m_current_multipart->end();
      delete m_current_multipart;
      m_current_multipart = nullptr;
    } else {
      Filter::output(evt);
    }
  }
}

auto MultipartDecoder::multipart_start(const std::string &content_type) -> Multipart* {
  if (content_type.length() > 1000) return nullptr;
  if (!utils::starts_with(content_type, s_multipart)) return nullptr;
  auto i = content_type.find(s_boundary);
  if (i == std::string::npos) return nullptr;
  i += s_boundary.length();
  return new Multipart(this, content_type.c_str() + i, content_type.length() - i);
}

//
// MultipartDecoder::Multipart
//

MultipartDecoder::Multipart::Multipart(MultipartDecoder *decoder, const char *boundary, int length)
  : m_decoder(decoder)
{
  char sep[length + 2];
  sep[0] = '-';
  sep[1] = '-';
  std::memcpy(sep + 2, boundary, length);
  m_kmp = new KMP(sep, length + 2);
  m_split = m_kmp->split([this](Data *data) { on_data(data); });
}

MultipartDecoder::Multipart::~Multipart() {
  delete m_child;
  delete m_split;
}

void MultipartDecoder::Multipart::parse(Data &data) {
  m_split->input(data);
}

void MultipartDecoder::Multipart::end() {
  m_split->end();
}

void MultipartDecoder::Multipart::on_data(Data *data) {
  if (!data) {
    if (m_state == BODY) {
      if (m_child) {
        m_child->end();
        delete m_child;
        m_child = nullptr;
      } else {
        m_decoder->Filter::output(MessageEnd::make());
      }
    }
    m_state = START;
    m_head = nullptr;
    m_header.clear();
  } else {
    while (!data->empty()) {
      auto state = m_state;
      Data buf; data->shift_to(
        [&](int c) {
          switch (state) {
            case START:
              if (c == '\r') state = CRLF;
              else if (c == '-') state = DASH;
              else state = END;
              break;
            case CRLF:
              if (c == '\n') state = HEADER;
              else state = END;
              return true;
            case DASH:
              state = END;
              break;
            case HEADER:
              if (c == '\n') {
                state = HEADER_EOL;
                return true;
              }
              break;
            default: break;
          }
          return false;
        },
        buf
      );

      // old state
      switch (m_state) {
        case HEADER:
          if (m_header.size() + buf.size() > MAX_HEADER_SIZE) {
            auto room = MAX_HEADER_SIZE - m_header.size();
            buf.pop(buf.size() - room);
          }
          m_header.push(std::move(buf));
          break;
        case BODY:
          if (m_child) {
            m_child->parse(buf);
          } else {
            m_decoder->Filter::output(Data::make(std::move(buf)));
          }
          break;
        default: break;
      }

      // new state
      switch (state) {
        case HEADER_EOL: {
          int len = m_header.size();
          if (len > 2) {
            char buf[len + 1];
            m_header.to_bytes((uint8_t *)buf);
            buf[len] = 0;
            if (auto p = std::strchr(buf, ':')) {
              std::string name(buf, p - buf);
              p++; while (*p && std::isblank(*p)) p++;
              if (auto q = std::strpbrk(p, "\r\n")) {
                std::string value(p, q - p);
                for (auto &c : name) c = std::tolower(c);
                pjs::Ref<pjs::Str> key(pjs::Str::make(name));
                pjs::Ref<pjs::Str> val(pjs::Str::make(value));
                if (!m_head) m_head = MessageHead::make();
                auto headers = m_head->headers();
                if (!headers) m_head->headers(headers = pjs::Object::make());
                pjs::Value existing;
                headers->get(key, existing);
                if (existing.is_undefined()) {
                  headers->set(key, val.get());
                } else if (existing.is_array()) {
                  existing.as<pjs::Array>()->push(val.get());
                } else {
                  auto *a = pjs::Array::make(2);
                  a->set(0, existing);
                  a->set(1, val.get());
                }
              }
            }
            state = HEADER;
          } else {
            pjs::Value content_type;
            if (m_head) {
              if (auto *headers = m_head->headers()) {
                headers->get(s_content_type, content_type);
              }
            }
            if (content_type.is_string()) m_child = m_decoder->multipart_start(content_type.s()->str());
            if (!m_child) m_decoder->Filter::output(MessageStart::make(m_head));
            state = BODY;
          }
          m_header.clear();
          break;
        }
        default: break;
      }

      m_state = state;
    }
  }
}

//
// MultipartEncoder
//

MultipartEncoder::MultipartEncoder()
  : m_prop_headers("headers")
{
}

MultipartEncoder::MultipartEncoder(const MultipartEncoder &r)
  : MultipartEncoder()
{
}

MultipartEncoder::~MultipartEncoder()
{
}

void MultipartEncoder::dump(Dump &d) {
  Filter::dump(d);
  d.name = "encodeMultipart";
}

auto MultipartEncoder::clone() -> Filter* {
  return new MultipartEncoder(*this);
}

void MultipartEncoder::reset() {
  Filter::reset();
}

void MultipartEncoder::process(Event *evt) {
}

} // namespace mime
} // namespace pipy

namespace pjs {

using namespace pipy::mime;

template<> void ClassDef<MessageHead>::init() {
  ctor();
  variable("headers", MessageHead::Field::headers);
}

} // namespace pjs
