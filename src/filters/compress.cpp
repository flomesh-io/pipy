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
#include "compressor.hpp"
#include "data.hpp"
#include "api/http.hpp"

namespace pipy {

thread_local static const pjs::ConstStr s_headers("headers");
thread_local static const pjs::ConstStr s_content_encoding("content-encoding");
thread_local static const pjs::ConstStr s_gzip("gzip");
thread_local static const pjs::ConstStr s_deflate("deflate");
thread_local static const pjs::ConstStr s_inflate("inflate");

static Data::Producer s_dp("compressMessage()");

//
// Compress
//

Compress::Compress(const pjs::Value &algorithm)
  : m_algorithm(algorithm)
{
}

Compress::Compress(const Compress &r)
  : Filter(r)
  , m_algorithm(r.m_algorithm)
{
}

Compress::~Compress()
{
}

void Compress::dump(Dump &d) {
  Filter::dump(d);
  d.name = "compress";
}

auto Compress::clone() -> Filter* {
  return new Compress(*this);
}

void Compress::reset() {
  Filter::reset();
  if (m_compressor) {
    m_compressor->finalize();
    m_compressor = nullptr;
  }
  m_is_started = false;
}

void Compress::process(Event *evt) {
  if (!m_is_started) {
    m_is_started = true;
    pjs::Value algorithm;
    if (!Filter::eval(m_algorithm, algorithm)) return;
    if (!algorithm.is_string()) {
      Filter::error("algorithm is not or did not return a string");
      return;
    }
    auto out = [this](Data &data) { compressor_output(data); };
    auto str = algorithm.s();
    if (str == s_deflate) {
      m_compressor = Compressor::deflate(out);
    } else if (str == s_gzip) {
      m_compressor = Compressor::gzip(out);
    } else {
      Filter::error("unknown compression algorithm: %s", str->c_str());
      return;
    }
  }

  if (m_compressor) {
    if (auto data = evt->as<Data>()) {
      m_compressor->input(*data, false);
    } else if (evt->is<StreamEnd>()) {
      m_compressor->flush();
      m_compressor->finalize();
      m_compressor = nullptr;
    }
  }

  if (evt->is<StreamEnd>()) {
    Filter::output(evt);
  }
}

void Compress::compressor_output(Data &data) {
  Filter::output(Data::make(std::move(data)));
}

//
// CompressHTTP
//


CompressHTTP::CompressHTTP(const pjs::Value &algorithm)
  : m_algorithm(algorithm)
{
}

CompressHTTP::CompressHTTP(const CompressHTTP &r)
  : Filter(r)
  , m_algorithm(r.m_algorithm)
{
}

CompressHTTP::~CompressHTTP()
{
}

void CompressHTTP::dump(Dump &d) {
  Filter::dump(d);
  d.name = "compressHTTP";
}

auto CompressHTTP::clone() -> Filter* {
  return new CompressHTTP(*this);
}

void CompressHTTP::reset() {
  Filter::reset();
  if (m_compressor) {
    m_compressor->finalize();
    m_compressor = nullptr;
  }
  m_is_message_started = false;
}

void CompressHTTP::process(Event *evt) {
  if (auto ms = evt->as<MessageStart>()) {
    if (!m_is_message_started) {
      pjs::Ref<pjs::Str> algorithm;
      if (m_algorithm.is_function()) {
        pjs::Value arg(ms), ret;
        if (!Filter::callback(m_algorithm.f(), 1, &arg, ret)) return;
        if (!ret.is_nullish()) {
          if (!ret.is_string()) {
            Filter::error("callback did not return a string");
            return;
          }
          algorithm = ret.s();
        }
      } else {
        if (!m_algorithm.is_string()) {
          Filter::error("algorithm expects a string");
          return;
        }
        algorithm = m_algorithm.s();
      }
      pjs::Ref<http::MessageHead> head = pjs::coerce<http::MessageHead>(ms->head());
      bool has_content_encoding = false;
      if (auto headers = head->headers.get()) {
        has_content_encoding = headers->has(s_content_encoding);
      }
      if (!has_content_encoding) {
        auto headers = head->headers.get();
        if (!headers) {
          headers = pjs::Object::make();
          if (!ms->head()) ms = MessageStart::make(pjs::Object::make());
          ms->head()->set(s_headers, headers);
        }
        auto out = [this](Data &data) { compressor_output(data); };
        if (algorithm == s_gzip) {
          m_compressor = Compressor::gzip(out);
          headers->set(s_content_encoding, s_gzip.get());
        } else if (algorithm == s_deflate) {
          m_compressor = Compressor::deflate(out);
          headers->set(s_content_encoding, s_deflate.get());
        }
      }
      m_is_message_started = true;
      Filter::output(ms);
    }

  } else if (auto data = evt->as<Data>()) {
    if (m_is_message_started) {
      if (m_compressor) {
        m_compressor->input(*data, false);
      } else {
        Filter::output(data);
      }
    }

  } else if (evt->is_end()) {
    if (m_is_message_started) {
      if (m_compressor) {
        m_compressor->flush();
        m_compressor->finalize();
        m_compressor = nullptr;
      }
      m_is_message_started = false;
      Filter::output(evt);
    }
  }
}

void CompressHTTP::compressor_output(Data &data) {
  Filter::output(Data::make(std::move(data)));
}

} // namespace pipy
