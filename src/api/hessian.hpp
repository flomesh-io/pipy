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

#ifndef HESSIAN_HPP
#define HESSIAN_HPP

#include "pjs/pjs.hpp"
#include "data.hpp"
#include "deframer.hpp"

namespace pipy {

class Data;

//
// Hessian
//

class Hessian : public pjs::ObjectTemplate<Hessian> {
public:
  static auto decode(const Data &data) -> pjs::Array*;
  static void encode(const pjs::Value &value, Data &data);
  static void encode(const pjs::Value &value, Data::Builder &db);

  //
  // Hessian::Collection
  //

  class Collection : public pjs::ObjectTemplate<Collection> {
  public:
    enum class Kind {
      list,
      map,
      class_def,
      object,
    };

    pjs::EnumValue<Kind> kind;
    pjs::Ref<pjs::Str> type;
    pjs::Ref<pjs::Object> elements;

  private:
    Collection(Kind k) : kind(k) {}
    friend class pjs::ObjectTemplate<Collection>;
  };

  //
  // Hessian::ReferenceMap
  //

  template<class T, int S = 10>
  class ReferenceMap {
  public:
    int add(T *obj) {
      if (m_size < S) {
        auto i = m_size;
        m_refs[m_size++] = obj;
        return i;
      } else {
        auto i = m_excessive.size() + S;
        m_excessive.push_back(obj);
        return i;
      }
    }

    auto get(int i) -> T* {
      if (0 <= i && i < m_size) return m_refs[i].get();
      if (S <= i && i < S + m_excessive.size()) return m_excessive[i-S].get();
      return nullptr;
    }

    auto find(T *obj) -> int {
      for (int i = 0; i < m_size; i++) if (m_refs[i] == obj) return i;
      for (int i = 0; i < m_excessive.size(); i++) if (m_excessive[i] == obj) return i + S;
      return -1;
    }

    void clear() {
      for (int i = 0; i < S; i++) m_refs[i] = nullptr;
      m_excessive.clear();
    }

  private:
    int m_size = 0;
    pjs::Ref<T> m_refs[S];
    std::vector<pjs::Ref<T>> m_excessive;
  };

  //
  // Hessian::Parser
  //

  class Parser : protected Deframer {
  public:
    Parser();

    void reset();
    void parse(Data &data);

  protected:
    virtual void on_message_start() {}
    virtual void on_message_end(const pjs::Value &value) = 0;

  private:
    enum State {
      ERROR = -1,
      START = 0,
      INT,
      LONG,
      DOUBLE,
      DOUBLE_8,
      DOUBLE_16,
      DOUBLE_32,
      DATE_32,
      DATE_64,
      STRING_SIZE,
      STRING_SIZE_FINAL,
      STRING_DATA,
      STRING_DATA_FINAL,
      BINARY_SIZE,
      BINARY_SIZE_FINAL,
      BINARY_DATA,
      BINARY_DATA_FINAL,
    };

    enum class CollectionState {
      VALUE,
      LENGTH,
      TYPE,
      TYPE_LENGTH,
      CLASS_DEF,
    };

    struct Level : public pjs::Pooled<Level> {
      Level *back;
      pjs::Ref<Collection> collection;
      pjs::Ref<Collection> class_def;
      CollectionState state;
      int length;
      int count = 0;
    };

    Level* m_stack = nullptr;
    pjs::Value m_root;
    pjs::Ref<Data> m_read_data;
    pjs::Utf8Decoder m_utf8_decoder;
    int m_utf8_length = 0;
    uint8_t m_read_number[8];
    ReferenceMap<Collection> m_obj_refs;
    ReferenceMap<Collection> m_def_refs;
    ReferenceMap<pjs::Str> m_type_refs;
    bool m_is_ref = false;

    virtual auto on_state(int state, int c) -> int override;

    void push_utf8_char(int c);
    auto push_string() -> State;
    auto push(
      const pjs::Value &value,
      CollectionState state = CollectionState::VALUE,
      int length = -1,
      Collection *class_def = nullptr
    ) -> State;
    void pop();
    void start();
    void end();
  };

  //
  // RESP::StreamParser
  //

  class StreamParser : public Parser {
  public:
    StreamParser(const std::function<void(const pjs::Value &)> &cb)
      : m_cb(cb) {}

    virtual void on_message_end(const pjs::Value &value) override {
      m_cb(value);
    }

  private:
    std::function<void(const pjs::Value &)> m_cb;
  };
};

} // namespace pipy

#endif // HESSIAN_HPP
