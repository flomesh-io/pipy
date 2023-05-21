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

#include "gui-tarball.hpp"
#include "compress.hpp"
#include "data.hpp"

#ifdef PIPY_USE_GUI
#include "gui.tar.h"
#endif

namespace pipy {

#ifdef PIPY_USE_GUI

thread_local static Data::Producer s_dp("GUI Tarball");
uint8_t *s_decompressed_gui_tar = nullptr;
size_t s_decompressed_gui_tar_size = 0;

static void decompress_gui_tar() {
  Data out;
  auto *decompressor = Decompressor::brotli(
    [&](Data &data) {
      out.push(std::move(data));
    }
  );

  decompressor->input(Data(s_gui_tar, sizeof(s_gui_tar), &s_dp));
  decompressor->end();
  s_decompressed_gui_tar = new uint8_t[out.size()];
  s_decompressed_gui_tar_size = out.size();
  out.to_bytes(s_decompressed_gui_tar);
}

auto GuiTarball::data() -> const char* {
  if (!s_decompressed_gui_tar) decompress_gui_tar();
  return (const char *)s_decompressed_gui_tar;
}

auto GuiTarball::size() -> size_t {
  if (!s_decompressed_gui_tar) decompress_gui_tar();
  return s_decompressed_gui_tar_size;
}

#else // !PIPY_USE_GUI

auto GuiTarball::data() -> const char* {
  return nullptr;
}

auto GuiTarball::size() -> size_t {
  return 0;
}

#endif // PIPY_USE_GUI

} // namespace pipy
