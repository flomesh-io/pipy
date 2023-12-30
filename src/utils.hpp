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

#ifndef UTILS_HPP
#define UTILS_HPP

#include "pjs/pjs.hpp"

#include <functional>
#include <list>
#include <map>
#include <stdexcept>
#include <string>

namespace pipy {
namespace utils {

auto to_string(double n) -> std::string;
auto to_string(char *str, size_t len, int n) -> size_t;
auto now() -> double;
bool is_host_port(const std::string &str);
bool get_host_port(const std::string &str, std::string &ip, int &port);
void get_prop_list(
  const std::string &str, char line_sep, char kv_sep,
  const std::function<void(const std::string &, const std::string &)> &cb
);
bool get_ip_v4(const std::string &str, uint8_t ip[]);
bool get_ip_v6(const std::string &str, uint8_t ip[]);
bool get_ip_v6(const std::string &str, uint16_t ip[]);
bool get_ip_v4(const char *str, uint8_t ip[]);
bool get_ip_v6(const char *str, uint8_t ip[]);
bool get_ip_v6(const char *str, uint16_t ip[]);
bool get_cidr(const std::string &str, uint8_t ip[], int &mask);
auto get_size(const std::string &str, int thousand = 1000) -> double;
auto get_binary_size(const std::string &str) -> double;
auto get_byte_size(const std::string &str) -> size_t;
bool get_byte_size(const pjs::Value &val, size_t &out);
auto get_seconds(const std::string &str) -> double;
bool get_seconds(const pjs::Value &val, double &out);
bool get_uuid(const std::string &str, uint8_t uuid[]);
auto make_uuid(const uint8_t uuid[]) -> std::string;
auto make_uuid_v4() -> std::string;
bool starts_with(const std::string &str, const std::string &prefix);
bool ends_with(const std::string &str, const std::string &suffix);
bool iequals(const char *a, const char *b, size_t n);
bool iequals(const std::string &a, const std::string &b);
auto trim(const std::string &str) -> std::string;
auto split(const std::string &str, char sep) -> std::list<std::string>;
auto lower(const std::string &str) -> std::string;
void escape(const std::string &str, const std::function<void(char)> &out);
auto escape(const std::string &str) -> std::string;
void unescape(const std::string &str, const std::function<void(char)> &out);
auto unescape(const std::string &str) -> std::string;
auto decode_uri(const std::string &str) -> std::string;
auto encode_uri(const std::string &str) -> std::string;
auto encode_hex(char *out, const void *inp, int len) -> int;
auto decode_hex(void *out, const char *inp, int len) -> int;
auto encode_base64(char *out, const void *inp, int len) -> int;
auto decode_base64(void *out, const char *inp, int len) -> int;
auto encode_base64url(char *out, const void *inp, int len) -> int;
auto decode_base64url(void *out, const char *inp, int len) -> int;
auto path_join(const std::string &base, const std::string &path) -> std::string;
auto path_normalize(const std::string &path) -> std::string;
auto path_dirname(const std::string &path) -> std::string;

//
// HexEncoder
//

class HexEncoder {
public:
  HexEncoder(const std::function<void(char)> &output)
    : m_output(output) {}

  static auto h2c(char h) -> char;

  void input(uint8_t b);

private:
  const std::function<void(char)> m_output;
};

//
// HexDecoder
//

class HexDecoder {
public:
  HexDecoder(const std::function<void(int)> &output)
    : m_output(output) {}

  static auto c2h(char c) -> char;

  bool input(char c);

private:
  const std::function<void(int)> m_output;
  uint8_t m_byte = 0;
  int m_shift = 0;
};

//
// Base64Encoder
//

class Base64Encoder {
public:
  static size_t max_output_size(size_t input_size) {
    return input_size * 4 / 3 + 4;
  }

  Base64Encoder(const std::function<void(char)> &output)
    : m_output(output) {}

  void input(uint8_t b);
  void flush();

private:
  const std::function<void(char)> m_output;
  uint32_t m_triplet = 0;
  int m_shift = 0;
};

//
// Base64Decoder
//

class Base64Decoder {
public:
  static size_t max_output_size(size_t input_size) {
    return input_size * 3 / 4 + 3;
  }

  Base64Decoder(const std::function<void(uint8_t)> &output)
    : m_output(output) {}

  bool input(char c);
  bool complete() { return !m_shift; }

private:
  const std::function<void(uint8_t)> m_output;
  uint32_t m_triplet = 0;
  int m_shift = 0;
  bool m_done = false;
};

//
// Base64UrlEncoder
//

class Base64UrlEncoder {
public:
  static size_t max_output_size(size_t input_size) {
    return input_size * 4 / 3 + 4;
  }

  Base64UrlEncoder(const std::function<void(char)> &output)
    : m_output(output) {}

  void input(uint8_t b);
  void flush();

private:
  const std::function<void(char)> m_output;
  uint32_t m_triplet = 0;
  int m_shift = 0;
};

//
// Base64UrlDecoder
//

class Base64UrlDecoder {
public:
  static size_t max_output_size(size_t input_size) {
    return input_size * 3 / 4 + 3;
  }

  Base64UrlDecoder(const std::function<void(uint8_t)> &output)
    : m_output(output) {}

  bool input(char c);
  bool flush();

private:
  const std::function<void(uint8_t)> m_output;
  uint32_t m_triplet = 0;
  int m_shift = 0;
  bool m_done = false;
};

//
// Utf16Encoder
//

class Utf16Encoder {
public:
  Utf16Encoder(bool big_endian, const std::function<void(uint8_t)> &output)
    : m_output(output)
    , m_big_endian(big_endian) {}

  void input(uint32_t ch);

private:
  const std::function<void(uint8_t)> m_output;
  bool m_big_endian;
};

//
// Utf16Decoder
//

class Utf16Decoder {
public:
  Utf16Decoder(bool big_endian, const std::function<void(uint32_t)> &output)
    : m_output(output)
    , m_big_endian(big_endian) {}

  void input(uint8_t b);
  void flush();

private:
  const std::function<void(uint32_t)> m_output;
  bool m_big_endian;
  bool m_has_half_word = false;
  uint16_t m_half_word = 0;
  uint16_t m_surrogate = 0;
};

} // namespace utils
} // namespace pipy

#endif // UTILS_HPP
