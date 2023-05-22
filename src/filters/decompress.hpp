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

#ifndef DECOMPRESS_HPP
#define DECOMPRESS_HPP

#include "filter.hpp"

namespace pipy {

class Decompressor;
class Data;

//
// Decompress
//

class Decompress : public Filter {
public:
  Decompress(const pjs::Value &algorithm);

private:
  Decompress(const Decompress &r);
  ~Decompress();

  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void process(Event *evt) override;
  virtual void dump(Dump &d) override;

  pjs::Value m_algorithm;
  Decompressor* m_decompressor = nullptr;
  bool m_is_started = false;

  void decompressor_output(Data &data);
};

//
// DecompressHTTP
//

class DecompressHTTP : public Filter {
public:
  DecompressHTTP();

private:
  DecompressHTTP(const DecompressHTTP &r);
  ~DecompressHTTP();

  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual void process(Event *evt) override;
  virtual void dump(Dump &d) override;

  Decompressor* m_decompressor = nullptr;
  bool m_is_message_started = false;

  void decompressor_output(Data &data);
};

} // namespace pipy

#endif // DECOMPRESS_HPP
