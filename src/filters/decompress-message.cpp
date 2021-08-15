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

#include "decompress-message.hpp"
#include "compress.hpp"
#include "data.hpp"
#include "logging.hpp"

namespace pipy {

//
// DecompressMessageBase
//

void DecompressMessageBase::reset() {
  if (m_decompressor) {
    m_decompressor->end();
    m_decompressor = nullptr;
  }
  m_message_started = false;
  m_session_end = false;
}

void DecompressMessageBase::process(Context *ctx, Event *inp) {
  if (m_session_end) return;

  if (auto *data = inp->as<Data>()) {
    if (m_message_started) {
      if (m_decompressor) {
        if (!m_decompressor->process(data)) {
          Log::warn("[decompress] decompression error");
          m_decompressor->end();
          m_decompressor = nullptr;
        }
      } else {
        output(inp);
      }
    }
    return;
  }

  if (auto start = inp->as<MessageStart>()) {
    if (!m_message_started) {
      m_decompressor = new_decompressor(ctx, start);
      m_message_started = true;
    }

  } else if (inp->is<MessageEnd>()) {
    if (m_decompressor) {
      m_decompressor->end();
      m_decompressor = nullptr;
    }
    m_message_started = false;

  } else if (inp->is<SessionEnd>()) {
    m_session_end = true;
  }

  output(inp);
}

//
// DecompressMessage
//

DecompressMessage::DecompressMessage()
{
}

DecompressMessage::DecompressMessage(const pjs::Value &algorithm)
  : m_algorithm(algorithm)
{
}

DecompressMessage::DecompressMessage(const DecompressMessage &r)
  : DecompressMessage(r.m_algorithm)
{
}

DecompressMessage::~DecompressMessage()
{
}

auto DecompressMessage::help() -> std::list<std::string> {
  return {
    "decompressMessage(algorithm)",
    "Decompresses message bodies",
    "algorithm = <string|function> One of the algorithms: inflate, ...",
  };
}

void DecompressMessage::dump(std::ostream &out) {
  out << "decompressMessage";
}

auto DecompressMessage::clone() -> Filter* {
  return new DecompressMessage(*this);
}

static const pjs::Ref<pjs::Str> s_inflate(pjs::Str::make("inflate"));

auto DecompressMessage::new_decompressor(Context *ctx, MessageStart *start) -> Decompressor* {
  pjs::Value algorithm;
  if (m_algorithm.is_function()) {
    pjs::Value msg(start);
    if (!callback(*ctx, m_algorithm.f(), 1, &msg, algorithm)) return nullptr;
  } else {
    algorithm = m_algorithm;
  }
  if (!algorithm.is_string()) return nullptr;
  auto s = algorithm.s();
  if (s == s_inflate) {
    return Decompressor::inflate(out());
  } else {
    Log::error("[decompress] unknown compression algorithm: %s", s->c_str());
    return nullptr;
  }
}

//
// DecompressHTTP
//

DecompressHTTP::DecompressHTTP()
{
}

DecompressHTTP::DecompressHTTP(pjs::Function *enable)
  : m_enable(enable)
{
}

DecompressHTTP::DecompressHTTP(const DecompressHTTP &r)
  : DecompressHTTP(r.m_enable)
{
}

DecompressHTTP::~DecompressHTTP()
{
}

auto DecompressHTTP::help() -> std::list<std::string> {
  return {
    "decompressHTTP([enable])",
    "Decompresses HTTP message bodies based on Content-Encoding header",
    "enable = <function> Returns true to decompress or false otherwise",
  };
}

void DecompressHTTP::dump(std::ostream &out) {
  out << "decompressHTTP";
}

auto DecompressHTTP::clone() -> Filter* {
  return new DecompressHTTP(*this);
}

static const pjs::Ref<pjs::Str> s_headers(pjs::Str::make("headers"));
static const pjs::Ref<pjs::Str> s_content_encoding(pjs::Str::make("content-encoding"));
static const pjs::Ref<pjs::Str> s_gzip(pjs::Str::make("gzip"));

auto DecompressHTTP::new_decompressor(Context *ctx, MessageStart *start) -> Decompressor* {
  auto head = start->head();
  if (!head) return nullptr;

  pjs::Value headers;
  head->get(s_headers, headers);
  if (!headers.is_object() || !headers.o()) return nullptr;

  pjs::Value content_encoding;
  headers.o()->get(s_content_encoding, content_encoding);
  if (!content_encoding.is_string()) return nullptr;

  auto is_enabled = [&]() -> bool {
    if (!m_enable) return true;
    pjs::Value ret, arg(start);
    if (!callback(*ctx, m_enable, 1, &arg, ret)) return false;
    return ret.to_boolean();
  };

  auto s = content_encoding.s();
  if (s == s_gzip) {
    if (is_enabled()) return Decompressor::inflate(out());
  }

  return nullptr;
}

} // namespace pipy