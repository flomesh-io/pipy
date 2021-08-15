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

#ifndef DECOMPRESS_MESSAGE_HPP
#define DECOMPRESS_MESSAGE_HPP

#include "filter.hpp"
#include "pjs/pjs.hpp"

namespace pipy {

class Decompressor;

//
// DecompressMessageBase
//

class DecompressMessageBase : public Filter {
protected:
  virtual auto new_decompressor(Context *ctx, MessageStart *start) -> Decompressor* = 0;

private:
  virtual void reset() override;
  virtual void process(Context *ctx, Event *inp) override;

  Decompressor* m_decompressor = nullptr;
  bool m_message_started = false;
  bool m_session_end = false;
};

//
// DecompressMessage
//

class DecompressMessage : public DecompressMessageBase {
public:
  DecompressMessage();
  DecompressMessage(const pjs::Value &algorithm);

private:
  DecompressMessage(const DecompressMessage &r);
  ~DecompressMessage();

  virtual auto help() -> std::list<std::string> override;
  virtual void dump(std::ostream &out) override;
  virtual auto clone() -> Filter* override;
  virtual auto new_decompressor(Context *ctx, MessageStart *start) -> Decompressor* override;

  pjs::Value m_algorithm;
};

//
// DecompressHTTP
//

class DecompressHTTP : public DecompressMessageBase {
public:
  DecompressHTTP();
  DecompressHTTP(pjs::Function *enable);

private:
  DecompressHTTP(const DecompressHTTP &r);
  ~DecompressHTTP();

  virtual auto help() -> std::list<std::string> override;
  virtual void dump(std::ostream &out) override;
  virtual auto clone() -> Filter* override;
  virtual auto new_decompressor(Context *ctx, MessageStart *start) -> Decompressor* override;

  pjs::Ref<pjs::Function> m_enable;
};

} // namespace pipy

#endif // DECOMPRESS_MESSAGE_HPP