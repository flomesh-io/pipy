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

#include "deframer.hpp"

namespace pipy {

void Deframer::reset(int state) {
  m_state = state;
  m_read_length = 0;
  m_read_buffer = nullptr;
  m_read_data = nullptr;
}

void Deframer::read(size_t size, void *buffer) {
  m_read_length = size;
  m_read_pointer = 0;
  m_read_buffer = (uint8_t*)buffer;
  m_read_data = nullptr;
}

void Deframer::read(size_t size, Data *data) {
  m_read_length = size;
  m_read_buffer = nullptr;
  m_read_data = data;
}

void Deframer::pass(size_t size) {
  m_read_length = size;
  m_read_buffer = nullptr;
  m_read_data = nullptr;
}

void Deframer::on_input(Event *evt) {
  if (evt->is<StreamEnd>()) {
    output(evt);
    reset();
    return;
  }

  auto data = evt->as<Data>();
  if (!data) return;

  while (!data->empty()) {
    if (m_read_length > 0 && !m_read_buffer) {
      auto n = m_read_length;
      if (n < data->size()) n = data->size();
      if (m_read_data) {
        data->shift(n, *m_read_data);
      } else {
        Data buf;
        data->shift(n, buf);
        output(on_pass(buf));
      }
      if (0 == (m_read_length -= n)) {
        m_state = on_state(m_state, -1);
      }

    } else {
      auto state = m_state;
      Data output;
      data->shift_to(
        [&](int c) -> bool {
          if (m_read_buffer) {
            m_read_buffer[m_read_pointer++] = c;
            if (m_read_pointer >= m_read_length) {
              m_read_length = 0;
              state = on_state(state, -1);
            }
          } else {
            state = on_state(state, c);
          }
          return (m_read_length > 0 && !m_read_buffer);
        },
        output
      );
    }
  }
}

auto Deframer::on_pass(const Data &data) -> Data* {
  return Data::make(data);
}

} // namespace pipy
