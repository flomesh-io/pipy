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

#ifndef API_PIPY_H
#define API_PIPY_H

#include "pjs/pjs.hpp"

namespace pipy {

class Pipy : public pjs::FunctionTemplate<Pipy> {
public:
  class Script : public pjs::ObjectTemplate<Script> {
  public:
    static void set(const std::string &path, const std::string &content);
    static void reset(const std::string &path);
  };

  class Store : public pjs::ObjectTemplate<Store> {
  public:
    static void set(const std::string &key, const std::string &value);
    static bool get(const std::string &key, std::string &value);

  private:
    static std::map<std::string, std::string> s_values;
  };

  void operator()(pjs::Context &ctx, pjs::Object *obj, pjs::Value &ret);
};

} // namespace pipy

#endif // API_PIPY_H