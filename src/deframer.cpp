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
  m_passing = false;
  m_read_length = 0;
  m_read_buffer = nullptr;
  m_read_data = nullptr;
  m_read_array = nullptr;
}

void Deframer::read(size_t size, void *buffer) {
  m_read_length = size;
  m_read_pointer = 0;
  m_read_buffer = (uint8_t*)buffer;
  m_read_data = nullptr;
  m_read_array = nullptr;
}

void Deframer::read(size_t size, Data *data) {
  m_read_length = size;
  m_read_buffer = nullptr;
  m_read_data = data;
  m_read_array = nullptr;
}

void Deframer::read(size_t size, pjs::Array *array) {
  m_read_length = size;
  m_read_buffer = nullptr;
  m_read_data = nullptr;
  m_read_array = array;
}

void Deframer::pass(size_t size) {
  m_read_length = size;
  m_read_buffer = nullptr;
  m_read_data = nullptr;
  m_read_array = nullptr;
}

void Deframer::pass_all(bool enable) {
  m_passing = enable;
}

void Deframer::deframe(Data &data) {
  Data output;
  while (!data.empty() && m_state >= 0) {

    if (m_read_length > 0 && !m_read_buffer) {
      auto n = m_read_length;
      if (n > data.size()) n = data.size();
      Data read_in;
      data.shift(n, read_in);
      if (m_passing) output.push(read_in);

      if (m_read_data) {
        m_read_data->push(read_in);
      } else if (m_read_array) {
        uint8_t buf[n];
        read_in.to_bytes(buf);
        for (int i = 0; i < n; i++) m_read_array->push(int(buf[i]));
      } else if (!m_passing) {
        output.push(read_in);
        on_pass(output);
        output.clear();
      }

      if (0 == (m_read_length -= n)) {
        m_state = on_state(m_state, -1);
      }

    } else {
      auto state = m_state;
      bool passing = m_passing;
      Data read_in;
      data.shift_to(
        [&](int c) -> bool {
          if (m_read_buffer) {
            m_read_buffer[m_read_pointer++] = c;
            if (m_read_pointer >= m_read_length) {
              m_read_length = 0;
              m_read_buffer = nullptr;
              state = on_state(state, -1);
            }
          } else {
            state = on_state(state, (uint8_t)c);
          }
          return state < 0 ||
            (m_read_length > 0 && !m_read_buffer) ||
            (m_passing != passing);
        },
        read_in
      );
      if (passing) output.push(read_in);
      m_state = state;
    }
  }

  if (!output.empty()) on_pass(output);
}

} // namespace pipy
