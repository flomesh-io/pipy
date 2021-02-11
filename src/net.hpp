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

#ifndef NET_HPP
#define NET_HPP

#include "object.hpp"
#include "data.hpp"

#define ASIO_STANDALONE
#include <asio.hpp>
#include <asio/ssl.hpp>

NS_BEGIN

extern asio::io_service g_io_service;

//
// Asio buffer array adapter
//

class DataChunks {
  Data::Chunks m_chunks;

public:
  class Iterator {
    Data::Chunks::Iterator m_i;

  public:
    Iterator(const Data::Chunks::Iterator &i) : m_i(i) {}
    bool operator==(const Iterator &other) const { return m_i == other.m_i; }
    bool operator!=(const Iterator &other) const { return m_i != other.m_i; }
    auto operator++() -> Iterator& { ++m_i; return *this; }
    auto operator*() -> asio::mutable_buffer {
      auto t = *m_i;
      return asio::mutable_buffer(
        std::get<0>(t),
        std::get<1>(t)
      );
    }
    auto operator*() const -> asio::const_buffer {
      auto t = *m_i;
      return asio::const_buffer(
        std::get<0>(t),
        std::get<1>(t)
      );
    }
  };

  DataChunks(const Data::Chunks &chunks) : m_chunks(chunks) {}
  auto begin() const -> Iterator { return Iterator(m_chunks.begin()); }
  auto end() const -> Iterator { return Iterator(m_chunks.end()); }
};

NS_END

template<>
inline auto asio::buffer_sequence_begin(
  const NS::DataChunks &chunks
) -> NS::DataChunks::Iterator {
  return chunks.begin();
}

template<>
inline auto asio::buffer_sequence_end(
  const NS::DataChunks &chunks
) -> NS::DataChunks::Iterator {
  return chunks.end();
}

template<>
class std::iterator_traits<NS::DataChunks::Iterator> {
public:
  typedef size_t difference_type;
};

template<>
inline void std::advance(NS::DataChunks::Iterator &i, size_t n) {
  while (n-- > 0) ++i;
}

template<>
class asio::is_const_buffer_sequence<NS::DataChunks> {
public:
  const static bool value = true;
};

#endif // NET_HPP
