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

#ifndef API_BPF_HPP
#define API_BPF_HPP

#include "pjs/pjs.hpp"
#include "api/c-struct.hpp"
#include "data.hpp"

namespace pipy {
namespace bpf {

//
// Map
//

class Map : public pjs::ObjectTemplate<Map> {
public:

  //
  // Map::Info
  //

  struct Info : public pjs::ObjectTemplate<Info> {
    pjs::Ref<pjs::Str> name;
    int id = 0;
    int flags = 0;
    int maxEntries = 0;
    int keySize = 0;
    int valueSize = 0;
  };

  static auto list() -> pjs::Array*;
  static auto open(int id, CStruct *key_type = nullptr, CStruct *value_type = nullptr) -> Map*;

  auto lookup(pjs::Object *key) -> pjs::Object*;
  void update(pjs::Object *key, pjs::Object *value);
  void remove(pjs::Object *key);
  void close();

private:
  Map(int fd, CStruct *key_type = nullptr, CStruct *value_type = nullptr);

  auto lookup_raw(Data *key) -> Data*;
  void update_raw(Data *key, Data *value);
  void delete_raw(Data *key);

  int m_fd;
  int m_key_size;
  int m_value_size;
  pjs::Ref<CStruct> m_key_type;
  pjs::Ref<CStruct> m_value_type;

  friend class pjs::ObjectTemplate<Map>;
};

//
// BPF
//

class BPF : public pjs::ObjectTemplate<BPF>
{
};

} // namespace bpf
} // namespace pipy

#endif // API_BPF_HPP
