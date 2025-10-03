#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include <brotli/encode.h>

#ifdef _WIN32

#include <Windows.h>

class Utf8Decoder {
public:
  static size_t encode(uint32_t code, char *output, size_t size) {
    if (code <= 0x7f) {
      if (size < 1) return 0;
      output[0] = code;
      return 1;
    } else if (code <= 0x7ff) {
      if (size < 2) return 0;
      output[0] = 0xc0 | (0x1f & (code >> 6));
      output[1] = 0x80 | (0x3f & (code >> 0));
      return 2;
    } else if (code <= 0xffff) {
      if (size < 3) return 0;
      output[0] = 0xe0 | (0x0f & (code >> 12));
      output[1] = 0x80 | (0x3f & (code >>  6));
      output[2] = 0x80 | (0x3f & (code >>  0));
      return 3;
    } else {
      if (size < 4) return 0;
      output[0] = 0xf0 | (0x07 & (code >> 18));
      output[1] = 0x80 | (0x3f & (code >> 12));
      output[2] = 0x80 | (0x3f & (code >>  6));
      output[3] = 0x80 | (0x3f & (code >>  0));
      return 4;
    }
  }

  Utf8Decoder(const std::function<void(int)> &output)
    : m_output(output) {}

  void reset() {
    m_codepoint = 0;
    m_shift = 0;
  }

  bool input(char c) {
    if (!m_shift) {
      if (c & 0x80) {
        if ((c & 0xe0) == 0xc0) { m_codepoint = c & 0x1f; m_shift = 1; } else
        if ((c & 0xf0) == 0xe0) { m_codepoint = c & 0x0f; m_shift = 2; } else
        if ((c & 0xf8) == 0xf0) { m_codepoint = c & 0x07; m_shift = 3; } else return false;
      } else {
        m_output(c);
      }
    } else {
      if ((c & 0xc0) != 0x80) {
        return false;
      }
      m_codepoint = (m_codepoint << 6) | (c & 0x3f);
      if (!--m_shift) m_output(m_codepoint);
    }
    return true;
  }

  bool end() {
    return !m_shift;
  }

private:
  const std::function<void(int)> m_output;
  uint32_t m_codepoint = 0;
  int m_shift = 0;
};

class Utf16Encoder {
public:
  Utf16Encoder(const std::function<void(wchar_t)> &output)
    : m_output_w(output) {}

  Utf16Encoder(bool big_endian, const std::function<void(uint8_t)> &output)
    : m_output_b(output)
    , m_big_endian(big_endian) {}

  void input(uint32_t ch) {
    if (ch <= 0xffff) {
      if (m_output_w) {
        m_output_w(ch);
      } else if (m_big_endian) {
        m_output_b(ch >> 8);
        m_output_b(ch & 0xff);
      } else {
        m_output_b(ch & 0xff);
        m_output_b(ch >> 8);
      }
    } else if (ch <= 0x10ffff) {
      ch -= 0x10000;
      uint16_t h = 0xd800 | (ch >> 10);
      uint16_t l = 0xdc00 | (ch & 0x3ff);
      if (m_output_w) {
        m_output_w(h);
        m_output_w(l);
      } else if (m_big_endian) {
        m_output_b(h >> 8);
        m_output_b(h & 0xff);
        m_output_b(l >> 8);
        m_output_b(l & 0xff);
      } else {
        m_output_b(h & 0xff);
        m_output_b(h >> 8);
        m_output_b(l & 0xff);
        m_output_b(l >> 8);
      }
    }
  }

private:
  const std::function<void(wchar_t)> m_output_w;
  const std::function<void(uint8_t)> m_output_b;
  bool m_big_endian;
};

class Utf16Decoder {
public:
  Utf16Decoder(const std::function<void(uint32_t)> &output)
    : m_output(output)
    , m_big_endian(false) {}

  Utf16Decoder(bool big_endian, const std::function<void(uint32_t)> &output)
    : m_output(output)
    , m_big_endian(big_endian) {}

  void input(char c) { input((uint8_t)c); }

  void input(uint8_t b) {
    if (m_has_half_word) {
      wchar_t w = m_half_word | (uint16_t(b) << (m_big_endian ? 0 : 8));
      input(w);
      m_has_half_word = false;
    } else {
      m_half_word = uint16_t(b) << (m_big_endian ? 8 : 0);
      m_has_half_word = true;
    }
  }

  void input(wchar_t w) {
    if (m_surrogate) {
      if ((w & 0xfc00) == 0xdc00) {
        uint32_t h = 0x3ff & m_surrogate;
        uint32_t l = 0x3ff & w;
        m_output(((h << 10) | l) + 0x10000);
        m_surrogate = 0;
      } else {
        m_output(m_surrogate);
        if ((w & 0xfc00) == 0xd800) {
          m_surrogate = w;
        } else {
          m_surrogate = 0;
          m_output(w);
        }
      }
    } else if ((w & 0xfc00) == 0xd800) {
      m_surrogate = w;
    } else {
      m_output(w);
    }
  }

  void flush() {
    if (auto w = m_surrogate) {
      m_surrogate = 0;
      m_output(w);
    }
  }

private:
  const std::function<void(uint32_t)> m_output;
  bool m_big_endian;
  bool m_has_half_word = false;
  uint16_t m_half_word = 0;
  uint16_t m_surrogate = 0;
};

auto a2w(const std::string &s) -> std::wstring {
  std::wstring buf;
  Utf16Encoder enc([&](wchar_t c) { buf.push_back(c); });
  Utf8Decoder dec([&](int c) { enc.input(c); });
  for (const auto c : s) dec.input(c);
  dec.end();
  return std::wstring(std::move(buf));
}

auto w2a(const std::wstring &s) -> std::string {
  std::string buf;
  Utf16Decoder dec(
    [&](uint32_t c) {
      char utf[5];
      auto len = Utf8Decoder::encode(c, utf, sizeof(utf));
      buf.append(utf, len);
    }
  );
  for (const auto c : s) dec.input(c);
  dec.flush();
  return std::string(std::move(buf));
}

auto convert_slash(const std::wstring &path) -> std::wstring {
  std::wstring copy(path);
  for (auto &c : copy) {
    if (c == '/') c = '\\';
  }
  return std::wstring(std::move(copy));
}

bool read_dir(const std::string &filename, std::vector<std::string> &list) {
  auto wpath = convert_slash(a2w(filename));
  if (wpath.back() != L'\\') wpath.push_back('\\');
  wpath.push_back(L'*');
  WIN32_FIND_DATAW data;
  HANDLE h = FindFirstFileW(wpath.c_str(), &data);
  if (h == INVALID_HANDLE_VALUE) return false;
  do {
    if (data.cFileName[0] == L'.') continue;
    auto name = w2a(data.cFileName);
    if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) name += '/';
    list.push_back(name);
  } while (FindNextFileW(h, &data));
  FindClose(h);
  return true;
}

#else // !_WIN32

#include <dirent.h>

bool read_dir(const std::string &filename, std::vector<std::string> &list) {
  if (DIR *dir = opendir(filename.c_str())) {
    while (auto *entry = readdir(dir)) {
      if (entry->d_name[0] == '.') continue;
      std::string name(entry->d_name);
      if (entry->d_type == DT_DIR) name += '/';
      list.push_back(name);
    }
    closedir(dir);
    return true;
  } else {
    return false;
  }
}

#endif // _WIN32

void usage() {
  std::cerr << "Usage: pack <output filename> <codebase list>" << std::endl;
  std::cerr << "<codebase list> = <group>[/<name>]:<pathname>,<group>[/<name>]:<pathname>,..." << std::endl;
}

bool starts_with(const std::string &str, const std::string &prefix) {
  if (str.length() < prefix.length()) return false;
  return !std::strncmp(str.c_str(), prefix.c_str(), prefix.length());
}

bool starts_with(const std::string &str, const std::vector<std::string> &prefixes) {
  for (const auto &prefix : prefixes) {
    if (!prefix.empty()) {
      if (starts_with(str, prefix)) {
        return true;
      }
    }
  }
  return false;
}

auto split(const std::string &str, char sep) -> std::vector<std::string> {
  std::vector<std::string> list;
  size_t p = 0;
  for (size_t i = 0; i < str.length(); i++) {
    if (str[i] == sep) {
      list.push_back(str.substr(p, i - p));
      p = i + 1;
    }
  }
  list.push_back(str.substr(p, str.length() - p));
  return list;
}

auto path_join(const std::string &base, const std::string &path) -> std::string {
  if (!base.empty() && base.back() == '/') {
    if (!path.empty() && path.front() == '/') {
      return base + path.substr(1);
    } else {
      return base + path;
    }
  } else {
    if (!path.empty() && path.front() == '/') {
      return base + path;
    } else {
      return base + '/' + path;
    }
  }
}

auto list_tree(const std::string &path) -> std::vector<std::string> {
  std::vector<std::string> list;
  std::function<void(const std::string &, const std::string &)> list_level;
  list_level = [&](const std::string &path, const std::string &base) {
    std::vector<std::string> names;
    if (read_dir(path, names)) {
      for (const auto &name : names) {
        if (name.back() == '/') {
          list_level(
            path_join(path, name),
            path_join(base, name)
          );
        } else {
          list.push_back(path_join(base, name));
        }
      }
    }
  };
  list_level(path, "/");
  return list;
}

bool read_file(const std::string &filename, std::vector<char> &data) {
  std::ifstream fs(filename, std::ios::in);
  if (!fs.is_open()) return false;
  char buf[1024];
  while (fs.good()) {
    fs.read(buf, sizeof(buf));
    data.insert(data.end(), buf, buf + fs.gcount());
  }
  return true;
}

auto compress(const std::vector<char> &input) -> std::vector<char> {
  auto output = new char[input.size()];
  size_t output_size = input.size();
  BrotliEncoderCompress(
    BROTLI_DEFAULT_QUALITY,
    BROTLI_DEFAULT_WINDOW,
    BROTLI_DEFAULT_MODE,
    input.size(),
    (const uint8_t *)input.data(),
    &output_size,
    (uint8_t *)output
  );
  std::vector<char> data(output, output + output_size);
  delete [] output;
  return data;
}

struct Codebase {
  std::string name;
  std::map<std::string, std::vector<char>> files;
};

int main(int argc, const char **argv) {
  if (argc < 3) {
    usage();
    return -1;
  }

  std::string output_filename(argv[1]);
  std::string codebase_list(argv[2]);
  std::string excluded_list = (argc > 3 ? argv[3] : "");

  std::cout << "Output: " << output_filename << std::endl;
  std::cout << "Codebase List: " << codebase_list << std::endl;
  std::cout << "Excluded List: " << excluded_list << std::endl;

  std::vector<Codebase> codebases;
  auto excluded = split(excluded_list, ',');

  for (const auto &item : split(codebase_list, ',')) {
    if (item.empty()) continue;

    auto segs = split(item, ':');
    if (segs.size() != 2) {
      usage();
      return -1;
    }

    auto name = segs[0];
    auto path = segs[1];

    auto p = name.find('/');
    if (p != std::string::npos) {
      if (p == 0 || p == name.length() - 1) {
        usage();
        return -1;
      }

      auto base_name = "/" + name;
      auto base_path = path;

      Codebase cb;
      cb.name = base_name;

      for (const auto &filename : list_tree(path)) {
        auto name = path_join(base_name, filename);
        auto path = path_join(base_path, filename);
        if (starts_with(name, excluded)) continue;
        if (!read_file(path, cb.files[name])) {
          std::cerr << "cannot read file: " << path << std::endl;
          return -1;
        }
      }

      codebases.push_back(std::move(cb));

    } else {
      std::vector<std::string> dirnames;
      if (!read_dir(path, dirnames)) {
        std::cerr << "cannot read dir: " << path << std::endl;
        return -1;
      }

      for (const auto &dirname : dirnames) {
        if (dirname.back() == '/') {
          auto base_name = path_join("/" + name, dirname);
          auto base_path = path_join(path, dirname);

          Codebase cb;
          cb.name = base_name;
          cb.name.pop_back();

          for (const auto &filename : list_tree(base_path)) {
            auto name = path_join(base_name, filename);
            auto path = path_join(base_path, filename);
            if (starts_with(name, excluded)) continue;
            if (!read_file(path, cb.files[name])) {
              std::cerr << "cannot read file: " << path << std::endl;
              return -1;
            }
          }

          codebases.push_back(std::move(cb));
        }
      }
    }
  }

  std::ofstream f(output_filename, std::ios::out | std::ios::trunc);
  if (!f.is_open()) {
    std::cerr << "cannot open file: " << output_filename << std::endl;
    return -1;
  }

  f << "#include <map>" << std::endl;
  f << "#include <string>" << std::endl << std::endl;

  for (size_t i = 0; i < codebases.size(); i++) {
    const auto &cb = codebases[i];
    std::vector<char> buffer;

    for (const auto &p : cb.files) {
      const auto &filename = p.first;
      const auto &data = p.second;
      std::string size = std::to_string(data.size());
      // std::cout << filename << std::endl;
      buffer.insert(buffer.end(), filename.begin(), filename.end());
      buffer.push_back('\0');
      buffer.insert(buffer.end(), size.begin(), size.end());
      buffer.push_back('\0');
      buffer.insert(buffer.end(), data.begin(), data.end());
    }

    std::cout << "Compressing codebase " << cb.name << "..." << std::flush;
    auto data = compress(buffer);
    std::cout << " down to size: " << data.size() << std::endl;

    f << "// Codebase " << cb.name << std::endl;
    f << "static const unsigned char s_codebase_" << i << "[" << std::to_string(data.size()) << "] = {" << std::endl;

    for (size_t row = 0; row < data.size(); row += 16) {
      f << ' ';
      for (size_t col = 0; col < 16; col++) {
        auto i = row + col;
        if (i >= data.size()) break;
        char hex[100];
        std::sprintf(hex, " 0x%02x,", (uint8_t)data[i]);
        f << hex;
      }
      f << std::endl;
    }

    f << "};" << std::endl << std::endl;
  }

  f << "// Codebase list" << std::endl;
  f << "static const std::map<std::string, std::pair<const unsigned char *, size_t>> s_codebases = {" << std::endl;

  for (size_t i = 0; i < codebases.size(); i++) {
    const auto &cb = codebases[i];
    f << "  { \"" << cb.name << "\", { s_codebase_" << i << ", sizeof(s_codebase_" << i << ") }}," << std::endl;
  }

  f << "};" << std::endl;

  return 0;
}
