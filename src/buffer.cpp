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

#include "buffer.hpp"

namespace pipy {

//
// Buffer::Options
//

Buffer::Options::Options(pjs::Object *options) {
  Value(options, "bufferLimit")
    .get_binary_size(bufferLimit)
    .check_nullable();
}

//
// Buffer
//

void Buffer::clear() {
  m_buffer.clear();
}

void Buffer::push(const Data &data) {
  if (data.empty()) return;
  m_buffer.push(data);
  if (m_options.bufferLimit >= 0 && m_buffer.size() > m_options.bufferLimit) {
    m_buffer.pop(m_buffer.size() - m_options.bufferLimit);
  }
}

auto Buffer::flush() -> Data* {
  return Data::make(std::move(m_buffer));
}

} // namespace pipy
