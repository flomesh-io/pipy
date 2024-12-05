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

#include "tee.hpp"
#include "fs.hpp"
#include "input.hpp"
#include "utils.hpp"

#include <algorithm>
#include <time.h>

#ifdef _WIN32

inline static void localtime_r(const time_t* timer, struct tm* buf) { localtime_s(buf, timer); }

#endif // _WIN32

namespace pipy {

//
// Tee::Options
//

Tee::Options::Options(pjs::Object *options) {
  Value(options, "shared")
    .get(shared)
    .check_nullable();
  Value(options, "append")
    .get(append)
    .check_nullable();
  Value(options, "maxFileSize")
    .get(max_file_size)
    .check_nullable();
  Value(options, "maxFileCount")
    .get(max_file_count)
    .check_nullable();
  Value(options, "rotateInterval")
    .get(rotate_interval)
    .check_nullable();
}

//
// Tee
//

Tee::Tee(const pjs::Value &filename, const Options &options)
  : m_options(options)
  , m_filename(filename)
{
}

Tee::Tee(const Tee &r)
  : Filter(r)
  , m_options(r.m_options)
  , m_filename(r.m_filename)
{
}

Tee::~Tee() {
}

void Tee::dump(Dump &d) {
  Filter::dump(d);
  d.name = "tee";
}

auto Tee::clone() -> Filter* {
  return new Tee(*this);
}

void Tee::reset() {
  Filter::reset();
  if (m_file) {
    m_file->close();
    m_file = nullptr;
  }
  m_target = nullptr;
  m_resolved_filename = nullptr;
}

void Tee::process(Event *evt) {
  if (auto *data = evt->as<Data>()) {
    if (!m_resolved_filename) {
      pjs::Value filename;
      if (!eval(m_filename, filename)) return;
      auto *s = filename.to_string();
      m_resolved_filename = s;
      s->release();
      if (m_options.shared) {
        m_target = get_target(m_resolved_filename->str(), m_options);
      } else {
        m_file = File::make(m_resolved_filename->str());
        m_file->open_write(m_options.append);
      }
    }

    if (m_file) {
      m_file->write(*data);
    } else if (m_target) {
      m_target->write(*data);
    }

  } else if (evt->is<StreamEnd>()) {
    if (m_file) {
      m_file->close();
      m_file = nullptr;
    }
  }

  output(evt);
}

std::map<std::string, pjs::Ref<Tee::Target>> Tee::s_targets;
std::mutex Tee::s_targets_mutex;

auto Tee::get_target(const std::string &filename, const Options &options) -> Target* {
  std::lock_guard<std::mutex> lock(s_targets_mutex);
  auto path = filename == "-" ? filename : fs::abs_path(filename);
  auto i = s_targets.find(path);
  if (i != s_targets.end()) return i->second;
  return s_targets[path] = new Target(path, options);
}

Tee::Target::Target(const std::string &filename, const Tee::Options &options)
  : m_filename(filename)
  , m_options(options)
{
}

Tee::Target::~Target() {

}

void Tee::Target::write(const Data &data) {
  if (Net::main().is_running()) {
    auto sd = SharedData::make(data)->retain();
    Net::main().post([=]() {
      Net::main().post(
        [=]() {
          Data data;
          sd->to_data(data);
          write_async(data);
          sd->release();
        }
      );
    });
  } else {
    write_async(data);
  }
}

void Tee::Target::write_async(const Data &data) {
  if (m_file && m_written_size > 0 && m_filename != "-" && (
    (m_options.max_file_size > 0 && m_written_size + data.size() > m_options.max_file_size) ||
    (m_options.rotate_interval > 0 && utils::now() - m_file_time > m_options.rotate_interval * 1000)
  )) {
    m_file->close();
    m_file = nullptr;

    auto sec = std::floor(m_file_time / 1000);
    auto t = std::time_t(sec);
    std::tm tm;
    localtime_r(&t, &tm);

    char str[100];
    auto len = std::strftime(str, sizeof(str), "%Y-%m-%d-%H-%M-%S-", &tm);
    auto date_filename = utils::path_join(
      utils::path_dirname(m_filename),
      std::string(str, len) + utils::path_basename(m_filename)
    );

    fs::rename(m_filename, date_filename);

    if (m_options.max_file_count > 0) {
      auto dirname = utils::path_dirname(m_filename);
      auto basename = utils::path_basename(m_filename);
      std::list<std::string> all;
      fs::read_dir(dirname, all);
      std::vector<std::string> names;
      for (const auto &name : all) {
        if (utils::ends_with(name, basename)) {
          names.push_back(name);
        }
      }
      if (names.size() > m_options.max_file_count) {
        std::sort(
          names.begin(), names.end(),
          [](const std::string &a, const std::string &b) -> bool { return a > b; }
        );
        while (names.size() > m_options.max_file_count) {
          auto name = names.back();
          names.pop_back();
          fs::unlink(utils::path_join(dirname, name));
        }
      }
    }
  }

  if (!m_file) {
    m_file_time = utils::now();
    m_written_size = 0;
    if (m_filename != "-") {
      fs::Stat st;
      if (fs::stat(m_filename, st) && st.is_file()) {
        m_file_time = st.ctime * 1000;
        m_written_size = st.size;
      }
    }
    m_file = File::make(m_filename);
    m_file->open_write(m_options.append);
  }

  InputContext ic;
  m_file->write(data);
  m_written_size += data.size();
}

} // namespace pipy
