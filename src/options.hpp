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

#ifndef OPTIONS_HPP
#define OPTIONS_HPP

#include "pjs/pjs.hpp"

#include <cstdio>
#include <string>

namespace pipy {

//
// Options
//

struct Options {

  //
  // Value
  //

  class Value {
  public:
    Value(pjs::Object *options, const char *name);

    Value& get(bool &value);
    Value& get(double &value, int thousand = 1000);
    Value& get(int &value, int thousand = 1000);
    Value& get(size_t &value, int thousand = 1000);
    Value& get(std::string &value);
    Value& get(pjs::Ref<pjs::Str> &value);
    Value& get(pjs::Ref<pjs::Function> &value);
    Value& get_binary_size(int &value);
    Value& get_binary_size(size_t &value);
    Value& get_seconds(double &value);

    void check();
    void check_nullable();

  private:
    enum Type {
      BOOLEAN,
      NUMBER,
      FINITE_NUMBER,
      POSITIVE_NUMBER,
      STRING,
      FUNCTION,
    };

    const char* m_name;
    pjs::Value m_value;
    Type m_types[10];
    size_t m_type_count = 0;
    bool m_got = false;

    void add_type(Type type);
    bool get_number(double &value, int thousand);
  };
};

} // namespace pipy

#endif // OPTIONS_HPP
