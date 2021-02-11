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

#ifndef CONFIG_HPP
#define CONFIG_HPP

#include "ns.hpp"
#include "logging.hpp"

#include <string>
#include <list>
#include <map>

NS_BEGIN

//
// Config
//

class Config {
public:
  struct Module {
    std::string name;
    std::map<std::string, std::string> params;
    int line;
  };

  struct Pipeline {
    std::string name;
    std::list<Module> modules;
    int line;
  };

  std::list<Pipeline> pipelines;

  bool parse_file(const std::string &pathname);
  bool parse_str(const char *str);
  void dump();
  void draw();

private:
  bool parse_line(const std::string &str, int num);
  void parse_error(const char *msg, int line);

  struct Context {
    int indent = 0;
    int level = 0;
    Pipeline *pipeline = nullptr;
    Module *module = nullptr;
    std::string header;
  };

  std::list<Context> m_parser_stack;
};

NS_END

#endif // CONFIG_HPP
