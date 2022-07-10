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

#include "kmp.hpp"

namespace pipy {

//
// Knuth-Morris-Pratt Algorithm
//
// LPS = Longest Proper Prefix that is also a Proper Suffix
//
// Word i      | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | ...
//    W[i]     | a | a | b | a | a | b | a | ? | ...
//  LPS i    0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | ...
//  LPS[i]  -1 | 0 | 1 | 0 | 1 | 2 | 3 | 4 | ? | ...
//
// Let i = Index of the last iteration
// Let j = LPS from the last iteration LPS[i]
//
// Solve LPS[8] based on LPS[7]:
//   i = 7,
//   j = LPS[7] = 4,
//   When W[i] == 'a':
//     LPS[8] = j + 1 = 5
//   When W[i] != 'b':
//     Find the second-best LPS in the earlier iterations:
//       Check W[LPS[j]]           => 'a' => still not 'b', go on ...
//       Check W[LPS[LPS[j]]       => 'a' => still not 'b', go on ...
//       Check W[LPS[LPS[LPS[j]]]] => out of range (j == -1) => LPS[8] = 0
//

KMP::KMP(const char *separator, size_t len)
  : m_pattern(pjs::PooledArray<char>::make(len))
  , m_lps_table(pjs::PooledArray<int>::make(len+1))
{
  auto *W = separator;
  auto *LPS = m_lps_table->elements();
  int i =  0; // Index of the last iteration
  int j = -1; // LPS from the last iteration LPS[i]
  LPS[i] = j; // First iteration is always -1
  while (i < len) {
    while (j >= 0 && W[i] != W[j]) {
      j = LPS[j];
    }
    i++; j++;
    LPS[i] = j;
  }
  std::memcpy(m_pattern->elements(), separator, len);
}

KMP::~KMP() {
  m_lps_table->free();
  m_pattern->free();
}

auto KMP::split(const std::function<void(Data*)> &output) -> Split* {
  return new Split(this, output);
}

//
// KMP::Split
//

void KMP::Split::input(Data &data) {
  const char *W = m_kmp->m_pattern->elements();
  const int *LPS = m_kmp->m_lps_table->elements();
  int n = m_kmp->m_pattern->size();
  int j = m_match_len;
  while (!data.empty()) {
    data.shift_to(
      [&](int c) {
        while (j >= 0 && c != W[j]) {
          j = LPS[j];
        }
        return (++j == n);
      },
      m_buffer
    );
    if (j == n) {
      m_buffer.pop(n);
      m_output(Data::make(std::move(m_buffer)));
      m_output(nullptr);
      j = 0;
    }
  }
  m_match_len = j;
  if (m_buffer.size() > j) {
    auto *data = Data::make();
    m_buffer.shift(m_buffer.size() - j, *data);
    m_output(data);
  }
}

void KMP::Split::end() {
  if (!m_buffer.empty()) {
    m_output(Data::make(std::move(m_buffer)));
  }
  m_output(nullptr);
}

} // namespace pipy
