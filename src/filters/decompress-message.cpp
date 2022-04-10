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

DecompressMessageBase::DecompressMessageBase()
{
}

DecompressMessageBase::DecompressMessageBase(const DecompressMessageBase &r)
  : Filter(r)
{
}

void DecompressMessageBase::reset() {
  Filter::reset();
  if (m_decompressor) {
    m_decompressor->end();
    m_decompressor = nullptr;
  }
  m_message_started = false;
}

void DecompressMessageBase::process(Event *evt) {
  if (auto *data = evt->as<Data>()) {
    if (m_message_started) {
      if (m_decompressor) {
        if (!m_decompressor->process(data)) {
          Log::warn("[decompress] decompression error");
          m_decompressor->end();
          m_decompressor = nullptr;
        }
      } else {
        output(evt);
      }
    }
    return;
  }

  if (auto start = evt->as<MessageStart>()) {
    if (!m_message_started) {
      m_decompressor = new_decompressor(
        start,
        [this](Data *data) {
          output(data);
        }
      );
      m_message_started = true;
    }

  } else if (evt->is<MessageEnd>()) {
    if (m_decompressor) {
      m_decompressor->end();
      m_decompressor = nullptr;
    }
    m_message_started = false;
  }

  output(evt);
}

//
// DecompressMessage
//

DecompressMessage::DecompressMessage(const pjs::Value &algorithm)
  : m_algorithm(algorithm)
{
}

DecompressMessage::DecompressMessage(const DecompressMessage &r)
  : DecompressMessageBase(r)
  , m_algorithm(r.m_algorithm)
{
}

DecompressMessage::~DecompressMessage()
{
}

void DecompressMessage::dump(std::ostream &out) {
  out << "decompressMessage";
}

auto DecompressMessage::clone() -> Filter* {
  return new DecompressMessage(*this);
}

static const pjs::Ref<pjs::Str> s_inflate(pjs::Str::make("inflate"));
static const pjs::Ref<pjs::Str> s_brotli(pjs::Str::make("brotli"));

auto DecompressMessage::new_decompressor(
  MessageStart *start,
  const std::function<void(Data*)> &out
) -> Decompressor* {
  pjs::Value algorithm;
  if (m_algorithm.is_function()) {
    pjs::Value msg(start);
    if (!callback(m_algorithm.f(), 1, &msg, algorithm)) return nullptr;
  } else {
    algorithm = m_algorithm;
  }
  if (!algorithm.is_string()) return nullptr;
  auto s = algorithm.s();
  if (s == s_inflate) {
    return Decompressor::inflate(out);
  } else if (s == s_brotli) {
    return Decompressor::brotli_dec(out);
  } else {
    Log::error("[decompress] unknown compression algorithm: %s", s->c_str());
    return nullptr;
  }
}

//
// DecompressHTTP
//

DecompressHTTP::DecompressHTTP(pjs::Function *enable)
  : m_enable(enable)
{
}

DecompressHTTP::DecompressHTTP(const DecompressHTTP &r)
  : DecompressMessageBase(r)
  , m_enable(r.m_enable)
{
}

DecompressHTTP::~DecompressHTTP()
{
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
static const pjs::Ref<pjs::Str> s_br(pjs::Str::make("br"));

auto DecompressHTTP::new_decompressor(
  MessageStart *start,
  const std::function<void(Data*)> &out
) -> Decompressor* {
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
    if (!callback(m_enable, 1, &arg, ret)) return false;
    return ret.to_boolean();
  };

  auto s = content_encoding.s();
  headers.o()->ht_delete(s);
  if (is_enabled()) {
    if (s == s_gzip) {
      return Decompressor::inflate(out);
    } else if (s == s_br) {
      return Decompressor::brotli_dec(out);
    }
  }

  return nullptr;
}

} // namespace pipy
