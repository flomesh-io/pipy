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

#include "compress-message.hpp"
#include "compress.hpp"
#include "data.hpp"
#include "logging.hpp"

namespace pipy {

//
// CompressMessageBase
//

CompressMessageBase::CompressMessageBase(const Options &options)
  : m_options(options)
{
}

CompressMessageBase::CompressMessageBase(const CompressMessageBase &r)
  : Filter(r)
  , m_options(r.m_options)
{
}

void CompressMessageBase::reset() {
  Filter::reset();
  if (m_compressor) {
    m_compressor->end();
    m_compressor = nullptr;
  }
  m_message_started = false;
}

void CompressMessageBase::process(Event *evt) {
  if (auto *data = evt->as<Data>()) {
    if (m_message_started) {
      if (m_compressor) {
        if (!m_compressor->process(data)) {
          Log::warn("[compress] compression error");
          m_compressor->end();
          m_compressor = nullptr;
        }
      } else {
        output(evt);
      }
    }
    return;
  }

  if (auto start = evt->as<MessageStart>()) {
    if (!m_message_started) {
      m_compressor = new_compressor(
        start,
        [this](Data *data) {
          output(data);
        }
      );
      m_message_started = true;
    }

  } else if (evt->is<MessageEnd>()) {
    if (m_compressor) {
      m_compressor->end();
      m_compressor = nullptr;
    }
    m_message_started = false;
  }

  output(evt);
}

static const pjs::Ref<pjs::Str> s_headers(pjs::Str::make("headers"));
static const pjs::Ref<pjs::Str> s_content_encoding(pjs::Str::make("content-encoding"));
static const pjs::Ref<pjs::Str> s_gzip(pjs::Str::make("gzip"));
static const pjs::Ref<pjs::Str> s_deflate(pjs::Str::make("deflate"));
static const pjs::Ref<pjs::Str> s_brtoli(pjs::Str::make("brotli"));

auto CompressMessageBase::new_compressor(
        MessageStart *start,
        const std::function<void(Data *)> &in
) -> Compressor * {
  if (!m_options.enable) return nullptr;

  auto head = start->head();
  if (!head) return nullptr;

  pjs::Ref<pjs::Str> method = m_options.algo == CompressionMethod::deflate ? s_deflate : (
          m_options.algo == CompressionMethod::gzip ? s_gzip : s_brtoli);
  pjs::Value headers;
  head->get(s_headers, headers);
  if (!headers.is_object() || !headers.o()) return nullptr;

  pjs::Value content_encoding;
  headers.o()->get(s_content_encoding, content_encoding);
  if ((!content_encoding.is_string()) || (content_encoding.s() != method)) {
    headers.o()->set(s_content_encoding, method.get());
  }

  switch (m_options.algo) {
    case CompressionMethod::deflate:
      return Compressor::deflate(in, static_cast<int>(m_options.level) - 1);
    case CompressionMethod::gzip:
      return Compressor::gzip(in, static_cast<int>(m_options.level) - 1);
    case CompressionMethod::brotli:
      return Compressor::brotli(in, static_cast<int>(m_options.level) - 1);
    default:
      Log::error("[compress] unknown compression algorithm: %s", m_options.algo);
      return nullptr;
  }
}

//
// CompressMessageBase::Options

CompressMessageBase::Options CompressMessageBase::Options::parse(pjs::Object *options) {
  CompressMessageBase::Options opts;
  if (options) {
    pjs::Value enable, method, level;
    options->get("enable", enable);
    if (!enable.is_undefined()) {
      if (!enable.is_boolean()) throw std::runtime_error("options.enable expects a boolean");
      opts.enable = enable.b();
    }
    options->get("method", method);
    if (!method.is_undefined()) {
      if (!method.is_string()) throw std::runtime_error("options.method requires a string");
      auto algo_type = pjs::EnumDef<CompressMessage::CompressionMethod>::value(method.s());
      if (int(algo_type) < 0)
        throw std::runtime_error("invalid options.method. It need to be one of [deflate | gzip | brotli]");
      opts.algo = algo_type;
    }
    options->get("level", level);
    if (!level.is_undefined()) {
      if (!level.is_string()) throw std::runtime_error("options.level requires a string");
      auto comp_level = pjs::EnumDef<CompressMessage::CompressionLevel>::value(level.s());
      if (int(comp_level) < 0)
        throw std::runtime_error("invalid options.level. It need to be one of [default | no | speed | best]");
      opts.level = comp_level;
    }
  }
  return opts;
}

//
// CompressMessage
//

CompressMessage::CompressMessage(const Options &options)
  : CompressMessageBase(options)
{
}

CompressMessage::CompressMessage(const CompressMessage &r)
  : CompressMessageBase(r)
{
}

CompressMessage::~CompressMessage()
{
}

void CompressMessage::dump(std::ostream &out) {
  out << "compressMessage";
}

auto CompressMessage::clone() -> Filter * {
  return new CompressMessage(*this);
}

//
// CompressHTTP
//

CompressHTTP::CompressHTTP(const Options &options)
  : CompressMessageBase(options)
{
}

CompressHTTP::CompressHTTP(const CompressHTTP &r)
  : CompressMessageBase(r)
{
}

CompressHTTP::~CompressHTTP() {
}

void CompressHTTP::dump(std::ostream &out) {
  out << "compressHTTP";
}

auto CompressHTTP::clone() -> Filter * {
  return new CompressHTTP(*this);
}
} // namespace pipy

//
// Algorithm
//
namespace pjs {
  using namespace pipy;

  template<>
  void EnumDef<CompressMessageBase::CompressionMethod>::init() {
    define(CompressMessageBase::CompressionMethod::deflate, "deflate");
    define(CompressMessageBase::CompressionMethod::gzip, "gzip");
    define(CompressMessageBase::CompressionMethod::brotli, "brotli");
  }

  template<>
  void EnumDef<CompressMessageBase::CompressionLevel>::init() {
    define(CompressMessageBase::CompressionLevel::Default, "default");
    define(CompressMessageBase::CompressionLevel::No, "no");
    define(CompressMessageBase::CompressionLevel::Speed, "speed");
    define(CompressMessageBase::CompressionLevel::Best, "best");
  }
}