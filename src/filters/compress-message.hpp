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

#ifndef COMPRESS_MESSAGE_HPP
#define COMPRESS_MESSAGE_HPP

#include "filter.hpp"
#include "options.hpp"

namespace pipy {

class Compressor;
class Data;

//
// CompressMessageBase
//

class CompressMessageBase : public Filter {
public:
    enum class Method {
      NO_COMPRESSION,
      DEFLATE,
      GZIP,
      BROTLI,
    };

    enum class Level {
      DEFAULT,
      SPEED,
      BEST,
    };

    struct Options : public pipy::Options {
      Method method = Method::NO_COMPRESSION;
      pjs::Ref<pjs::Function> method_f;
      Level level = Level::DEFAULT;
      pjs::Ref<pjs::Function> level_f;

      Options() {}
      Options(pjs::Object *options);
    };

protected:
  CompressMessageBase(const Options &options);
  CompressMessageBase(const CompressMessageBase &r);

  virtual auto new_compressor(
    MessageStart *start,
    Method &method,
    Level &level,
    const std::function<void(Data&)> &out
  ) -> Compressor*;

private:
  virtual void reset() override;
  virtual void process(Event *evt) override;

  Options m_options;
  Compressor* m_compressor = nullptr;
  std::function<void(Data&)> m_output;
  bool m_message_started = false;
};

//
// CompressMessage
//

class CompressMessage : public CompressMessageBase {
public:
  CompressMessage(const Options &options);

private:
  CompressMessage(const CompressMessage &r);
  ~CompressMessage();

  virtual auto clone() -> Filter* override;
  virtual void dump(Dump &d) override;
};

//
// CompressHTTP
//

class CompressHTTP : public CompressMessageBase {
public:
  CompressHTTP(const Options &options);

private:
  CompressHTTP(const CompressHTTP &r);
  ~CompressHTTP();

  virtual auto clone() -> Filter* override;
  virtual void dump(Dump &d) override;

  virtual auto new_compressor(
    MessageStart *start,
    Method &method,
    Level &level,
    const std::function<void(Data&)> &out
  ) -> Compressor* override;
};

} // namespace pipy

#endif //COMPRESS_MESSAGE_HPP
