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

#include "decompress.hpp"
#include "compressor.hpp"
#include "data.hpp"
#include "api/http.hpp"

namespace pipy {

thread_local static const pjs::ConstStr s_content_encoding("content-encoding");
thread_local static const pjs::ConstStr s_gzip("gzip");
thread_local static const pjs::ConstStr s_br("br");
thread_local static const pjs::ConstStr s_deflate("deflate");
thread_local static const pjs::ConstStr s_inflate("inflate");
thread_local static const pjs::ConstStr s_brotli("brotli");

//
// Decompress
//

Decompress::Decompress(const pjs::Value &algorithm)
  : m_algorithm(algorithm)
{
}

Decompress::Decompress(const Decompress &r)
  : Filter(r)
  , m_algorithm(r.m_algorithm)
{
}

Decompress::~Decompress()
{
}

void Decompress::dump(Dump &d) {
  Filter::dump(d);
  d.name = "decompress";
}

auto Decompress::clone() -> Filter* {
  return new Decompress(*this);
}

void Decompress::reset() {
  Filter::reset();
  if (m_decompressor) {
    m_decompressor->finalize();
    m_decompressor = nullptr;
  }
  m_is_started = false;
}

void Decompress::process(Event *evt) {
  if (!m_is_started) {
    m_is_started = true;
    pjs::Value algorithm;
    if (!Filter::eval(m_algorithm, algorithm)) return;
    if (!algorithm.is_string()) {
      Filter::error("algorithm is not or did not return a string");
      return;
    }
    auto out = [this](Data &data) { decompressor_output(data); };
    auto str = algorithm.s();
    if (str == s_inflate) {
      m_decompressor = Decompressor::inflate(out);
    } else if (str == s_brotli) {
      m_decompressor = Decompressor::brotli(out);
    } else {
      Filter::error("unknown compression algorithm: %s", str->c_str());
      return;
    }
  }

  if (m_decompressor) {
    if (auto data = evt->as<Data>()) {
      m_decompressor->input(*data);
    } else if (evt->is<StreamEnd>()) {
      m_decompressor->finalize();
      m_decompressor = nullptr;
    }
  }

  if (evt->is<StreamEnd>()) {
    Filter::output(evt);
  }
}

void Decompress::decompressor_output(Data &data) {
  Filter::output(Data::make(std::move(data)));
}

//
// DecompressHTTP
//

DecompressHTTP::DecompressHTTP()
{
}

DecompressHTTP::DecompressHTTP(const DecompressHTTP &r)
  : Filter(r)
{
}

DecompressHTTP::~DecompressHTTP()
{
}

void DecompressHTTP::dump(Dump &d) {
  Filter::dump(d);
  d.name = "decompressHTTP";
}

auto DecompressHTTP::clone() -> Filter* {
  return new DecompressHTTP(*this);
}

void DecompressHTTP::reset() {
  Filter::reset();
  if (m_decompressor) {
    m_decompressor->finalize();
    m_decompressor = nullptr;
  }
  m_is_message_started = false;
}

void DecompressHTTP::process(Event *evt) {
  if (auto ms = evt->as<MessageStart>()) {
    if (!m_is_message_started) {
      pjs::Ref<http::MessageHead> head = pjs::coerce<http::MessageHead>(ms->head());
      if (auto headers = head->headers.get()) {
        pjs::Value v;
        if (headers->get(s_content_encoding, v) && v.is_string()) {
          auto str = v.s();
          auto out = [this](Data &data) { decompressor_output(data); };
          if (str == s_gzip) m_decompressor = Decompressor::gzip(out);
          if (str == s_deflate) m_decompressor = Decompressor::inflate(out);
          else if (str == s_br) m_decompressor = Decompressor::brotli(out);
          if (m_decompressor) head->headers->ht_delete(s_content_encoding);
        }
      }
      m_is_message_started = true;
      Filter::output(evt);
    }

  } else if (auto data = evt->as<Data>()) {
    if (m_is_message_started) {
      if (m_decompressor) {
        m_decompressor->input(*data);
      } else {
        Filter::output(data);
      }
    }

  } else if (evt->is_end()) {
    if (m_is_message_started) {
      if (m_decompressor) {
        m_decompressor->finalize();
        m_decompressor = nullptr;
      }
      m_is_message_started = false;
      Filter::output(evt);
    }
  }
}

void DecompressHTTP::decompressor_output(Data &data) {
  Filter::output(Data::make(std::move(data)));
}

} // namespace pipy
