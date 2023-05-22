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

#ifndef COMPRESSOR_HPP
#define COMPRESSOR_HPP

#include <cstddef>
#include <functional>

namespace pipy {

class Data;

//
// Decompressor
//

class Decompressor {
public:
  typedef std::function<void(Data&)> Output;

  static Decompressor* inflate(const Output &out);
  static Decompressor* gzip(const Output &out);
  static Decompressor* brotli(const Output &out);

  virtual bool input(const Data &data) = 0;
  virtual bool finalize() = 0;

protected:
  ~Decompressor() {}
};

//
// Compressor
//

class Compressor {
public:
  typedef std::function<void(Data&)> Output;

  static Compressor* deflate(const Output &out);
  static Compressor* gzip(const Output &out);

  virtual bool input(const Data &data, bool flush) = 0;
  virtual bool flush() = 0;
  virtual bool finalize() = 0;

protected:
  ~Compressor() {}
};
} // namespace pipy

#endif // COMPRESSOR_HPP
