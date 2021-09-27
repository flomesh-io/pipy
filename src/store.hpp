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

#ifndef STORE_HPP
#define STORE_HPP

#include "data.hpp"

#include <set>
#include <ostream>

namespace pipy {

class Store {
public:
  static Store* open_memory();
  static Store* open_level_db(const std::string &path);

  class Batch {
  public:
    virtual void set(const std::string &key, const Data &data) = 0;
    virtual void erase(const std::string &key) = 0;
    virtual void commit() = 0;
    virtual void cancel() = 0;
  };

  virtual void keys(const std::string &base_key, std::set<std::string> &keys) = 0;
  virtual bool get(const std::string &key, Data &data) = 0;
  virtual void set(const std::string &key, const Data &data) = 0;
  virtual void erase(const std::string &key) = 0;
  virtual auto batch() -> Batch* = 0;
  virtual void close() = 0;
  virtual void dump(std::ostream &out) = 0;
};

} // namespace pipy

#endif // STORE_HPP