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

#include "zlib.hpp"
#include "compressor.hpp"

namespace pipy {

void ZLib::deflate(const Data &input, Data &output) {
  auto *compressor = Compressor::deflate(
    [&](Data &data) {
      output.push(data);
    }
  );
  auto ret = compressor->input(input, false) && compressor->flush();
  compressor->finalize();
  if (!ret) throw std::runtime_error("deflate() failed");
}

void ZLib::inflate(const Data &input, Data &output) {
  auto *decompressor = Decompressor::inflate(
    [&](Data &data) {
      output.push(data);
    }
  );
  auto ret = decompressor->input(input);
  decompressor->finalize();
  if (!ret) throw std::runtime_error("inflate() failed");
}

void ZLib::gzip(const Data &input, Data &output) {
  auto *compressor = Compressor::gzip(
    [&](Data &data) {
      output.push(data);
    }
  );
  auto ret = compressor->input(input, false) && compressor->flush();
  compressor->finalize();
  if (!ret) throw std::runtime_error("gzip() failed");
}

void ZLib::gunzip(const Data &input, Data &output) {
  auto *decompressor = Decompressor::gzip(
    [&](Data &data) {
      output.push(data);
    }
  );
  auto ret = decompressor->input(input);
  decompressor->finalize();
  if (!ret) throw std::runtime_error("gunzip() failed");
}

} // namespace pipy

namespace pjs {

using namespace pipy;

//
// ZLib
//

template<> void ClassDef<ZLib>::init() {
  ctor();

  method("deflate", [](Context &ctx, Object *obj, Value &ret) {
    pipy::Data *data;
    if (!ctx.arguments(1, &data)) return;
    if (!data) { ret = Value::null; return; }
    try {
      pipy::Data output;
      ZLib::deflate(*data, output);
      ret.set(pipy::Data::make(std::move(output)));
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  method("inflate", [](Context &ctx, Object *obj, Value &ret) {
    pipy::Data *data;
    if (!ctx.arguments(1, &data)) return;
    if (!data) { ret = Value::null; return; }
    try {
      pipy::Data output;
      ZLib::inflate(*data, output);
      ret.set(pipy::Data::make(std::move(output)));
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  method("gzip", [](Context &ctx, Object *obj, Value &ret) {
    pipy::Data *data;
    if (!ctx.arguments(1, &data)) return;
    if (!data) { ret = Value::null; return; }
    try {
      pipy::Data output;
      ZLib::gzip(*data, output);
      ret.set(pipy::Data::make(std::move(output)));
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  method("gunzip", [](Context &ctx, Object *obj, Value &ret) {
    pipy::Data *data;
    if (!ctx.arguments(1, &data)) return;
    if (!data) { ret = Value::null; return; }
    try {
      pipy::Data output;
      ZLib::gunzip(*data, output);
      ret.set(pipy::Data::make(std::move(output)));
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });
}

} // namespace pjs
