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

#include "http2.hpp"

namespace pipy {
namespace http2 {

static Data::Producer s_dp("HTTP2 Codec");

//
// HPACK static table
//

static const pjs::Ref<pjs::Str> s_colon_scheme(pjs::Str::make(":scheme"));
static const pjs::Ref<pjs::Str> s_colon_method(pjs::Str::make(":method"));
static const pjs::Ref<pjs::Str> s_colon_path(pjs::Str::make(":path"));
static const pjs::Ref<pjs::Str> s_colon_status(pjs::Str::make(":status"));
static const pjs::Ref<pjs::Str> s_colon_authority(pjs::Str::make(":authority"));
static const pjs::Ref<pjs::Str> s_method(pjs::Str::make("method"));
static const pjs::Ref<pjs::Str> s_scheme(pjs::Str::make("scheme"));
static const pjs::Ref<pjs::Str> s_authority(pjs::Str::make("authority"));
static const pjs::Ref<pjs::Str> s_host(pjs::Str::make("host"));
static const pjs::Ref<pjs::Str> s_path(pjs::Str::make("path"));
static const pjs::Ref<pjs::Str> s_status(pjs::Str::make("status"));
static const pjs::Ref<pjs::Str> s_headers(pjs::Str::make("headers"));
static const pjs::Ref<pjs::Str> s_http(pjs::Str::make("http"));
static const pjs::Ref<pjs::Str> s_GET(pjs::Str::make("GET"));
static const pjs::Ref<pjs::Str> s_CONNECT(pjs::Str::make("CONNECT"));
static const pjs::Ref<pjs::Str> s_root_path(pjs::Str::make("/"));
static const pjs::Ref<pjs::Str> s_200(pjs::Str::make("200"));
static const pjs::ConstStr s_http2_settings("http2-settings");
static const pjs::ConstStr s_connection("connection");
static const pjs::ConstStr s_keep_alive("keep-alive");

static struct {
  const char *name;
  const char *value;
} s_hpack_static_table[] = {
  { ":authority"                  , nullptr },
  { ":method"                     , "GET" },
  { ":method"                     , "POST" },
  { ":path"                       , "/" },
  { ":path"                       , "/index.html" },
  { ":scheme"                     , "http" },
  { ":scheme"                     , "https" },
  { ":status"                     , "200" },
  { ":status"                     , "204" },
  { ":status"                     , "206" },
  { ":status"                     , "304" },
  { ":status"                     , "400" },
  { ":status"                     , "404" },
  { ":status"                     , "500" },
  { "accept-charset"              , nullptr },
  { "accept-encoding"             , "gzip, deflate" },
  { "accept-language"             , nullptr }, 
  { "accept-ranges"               , nullptr }, 
  { "accept"                      , nullptr }, 
  { "access-control-allow-origin" , nullptr }, 
  { "age"                         , nullptr }, 
  { "allow"                       , nullptr }, 
  { "authorization"               , nullptr }, 
  { "cache-control"               , nullptr }, 
  { "content-disposition"         , nullptr }, 
  { "content-encoding"            , nullptr }, 
  { "content-language"            , nullptr }, 
  { "content-length"              , nullptr }, 
  { "content-location"            , nullptr }, 
  { "content-range"               , nullptr }, 
  { "content-type"                , nullptr }, 
  { "cookie"                      , nullptr }, 
  { "date"                        , nullptr }, 
  { "etag"                        , nullptr }, 
  { "expect"                      , nullptr }, 
  { "expires"                     , nullptr }, 
  { "from"                        , nullptr }, 
  { "host"                        , nullptr }, 
  { "if-match"                    , nullptr }, 
  { "if-modified-since"           , nullptr }, 
  { "if-none-match"               , nullptr }, 
  { "if-range"                    , nullptr }, 
  { "if-unmodified-since"         , nullptr }, 
  { "last-modified"               , nullptr }, 
  { "link"                        , nullptr }, 
  { "location"                    , nullptr }, 
  { "max-forwards"                , nullptr }, 
  { "proxy-authenticate"          , nullptr }, 
  { "proxy-authorization"         , nullptr }, 
  { "range"                       , nullptr }, 
  { "referer"                     , nullptr }, 
  { "refresh"                     , nullptr }, 
  { "retry-after"                 , nullptr }, 
  { "server"                      , nullptr }, 
  { "set-cookie"                  , nullptr }, 
  { "strict-transport-security"   , nullptr }, 
  { "transfer-encoding"           , nullptr }, 
  { "user-agent"                  , nullptr }, 
  { "vary"                        , nullptr }, 
  { "via"                         , nullptr }, 
  { "www-authenticate"            , nullptr }, 
};

//
// HPACK Huffman code table
//

static struct {
  uint32_t code;
  int bits;
} s_hpack_huffman_table[] = {
  { 0x00001ff8, 13 }, //     (  0)  |11111111|11000
  { 0x007fffd8, 23 }, //     (  1)  |11111111|11111111|1011000
  { 0x0fffffe2, 28 }, //     (  2)  |11111111|11111111|11111110|0010
  { 0x0fffffe3, 28 }, //     (  3)  |11111111|11111111|11111110|0011
  { 0x0fffffe4, 28 }, //     (  4)  |11111111|11111111|11111110|0100
  { 0x0fffffe5, 28 }, //     (  5)  |11111111|11111111|11111110|0101
  { 0x0fffffe6, 28 }, //     (  6)  |11111111|11111111|11111110|0110
  { 0x0fffffe7, 28 }, //     (  7)  |11111111|11111111|11111110|0111
  { 0x0fffffe8, 28 }, //     (  8)  |11111111|11111111|11111110|1000
  { 0x00ffffea, 24 }, //     (  9)  |11111111|11111111|11101010
  { 0x3ffffffc, 30 }, //     ( 10)  |11111111|11111111|11111111|111100
  { 0x0fffffe9, 28 }, //     ( 11)  |11111111|11111111|11111110|1001
  { 0x0fffffea, 28 }, //     ( 12)  |11111111|11111111|11111110|1010
  { 0x3ffffffd, 30 }, //     ( 13)  |11111111|11111111|11111111|111101
  { 0x0fffffeb, 28 }, //     ( 14)  |11111111|11111111|11111110|1011
  { 0x0fffffec, 28 }, //     ( 15)  |11111111|11111111|11111110|1100
  { 0x0fffffed, 28 }, //     ( 16)  |11111111|11111111|11111110|1101
  { 0x0fffffee, 28 }, //     ( 17)  |11111111|11111111|11111110|1110
  { 0x0fffffef, 28 }, //     ( 18)  |11111111|11111111|11111110|1111
  { 0x0ffffff0, 28 }, //     ( 19)  |11111111|11111111|11111111|0000
  { 0x0ffffff1, 28 }, //     ( 20)  |11111111|11111111|11111111|0001
  { 0x0ffffff2, 28 }, //     ( 21)  |11111111|11111111|11111111|0010
  { 0x3ffffffe, 30 }, //     ( 22)  |11111111|11111111|11111111|111110
  { 0x0ffffff3, 28 }, //     ( 23)  |11111111|11111111|11111111|0011
  { 0x0ffffff4, 28 }, //     ( 24)  |11111111|11111111|11111111|0100
  { 0x0ffffff5, 28 }, //     ( 25)  |11111111|11111111|11111111|0101
  { 0x0ffffff6, 28 }, //     ( 26)  |11111111|11111111|11111111|0110
  { 0x0ffffff7, 28 }, //     ( 27)  |11111111|11111111|11111111|0111
  { 0x0ffffff8, 28 }, //     ( 28)  |11111111|11111111|11111111|1000
  { 0x0ffffff9, 28 }, //     ( 29)  |11111111|11111111|11111111|1001
  { 0x0ffffffa, 28 }, //     ( 30)  |11111111|11111111|11111111|1010
  { 0x0ffffffb, 28 }, //     ( 31)  |11111111|11111111|11111111|1011
  { 0x00000014,  6 }, // ' ' ( 32)  |010100
  { 0x000003f8, 10 }, // '!' ( 33)  |11111110|00
  { 0x000003f9, 10 }, // '"' ( 34)  |11111110|01
  { 0x00000ffa, 12 }, // '#' ( 35)  |11111111|1010
  { 0x00001ff9, 13 }, // '$' ( 36)  |11111111|11001
  { 0x00000015,  6 }, // '%' ( 37)  |010101
  { 0x000000f8,  8 }, // '&' ( 38)  |11111000
  { 0x000007fa, 11 }, // ''' ( 39)  |11111111|010
  { 0x000003fa, 10 }, // '(' ( 40)  |11111110|10
  { 0x000003fb, 10 }, // ')' ( 41)  |11111110|11
  { 0x000000f9,  8 }, // '*' ( 42)  |11111001
  { 0x000007fb, 11 }, // '+' ( 43)  |11111111|011
  { 0x000000fa,  8 }, // ',' ( 44)  |11111010
  { 0x00000016,  6 }, // '-' ( 45)  |010110
  { 0x00000017,  6 }, // '.' ( 46)  |010111
  { 0x00000018,  6 }, // '/' ( 47)  |011000
  { 0x00000000,  5 }, // '0' ( 48)  |00000
  { 0x00000001,  5 }, // '1' ( 49)  |00001
  { 0x00000002,  5 }, // '2' ( 50)  |00010
  { 0x00000019,  6 }, // '3' ( 51)  |011001
  { 0x0000001a,  6 }, // '4' ( 52)  |011010
  { 0x0000001b,  6 }, // '5' ( 53)  |011011
  { 0x0000001c,  6 }, // '6' ( 54)  |011100
  { 0x0000001d,  6 }, // '7' ( 55)  |011101
  { 0x0000001e,  6 }, // '8' ( 56)  |011110
  { 0x0000001f,  6 }, // '9' ( 57)  |011111
  { 0x0000005c,  7 }, // ':' ( 58)  |1011100
  { 0x000000fb,  8 }, // ';' ( 59)  |11111011
  { 0x00007ffc, 15 }, // '<' ( 60)  |11111111|1111100
  { 0x00000020,  6 }, // '=' ( 61)  |100000
  { 0x00000ffb, 12 }, // '>' ( 62)  |11111111|1011
  { 0x000003fc, 10 }, // '?' ( 63)  |11111111|00
  { 0x00001ffa, 13 }, // '@' ( 64)  |11111111|11010
  { 0x00000021,  6 }, // 'A' ( 65)  |100001
  { 0x0000005d,  7 }, // 'B' ( 66)  |1011101
  { 0x0000005e,  7 }, // 'C' ( 67)  |1011110
  { 0x0000005f,  7 }, // 'D' ( 68)  |1011111
  { 0x00000060,  7 }, // 'E' ( 69)  |1100000
  { 0x00000061,  7 }, // 'F' ( 70)  |1100001
  { 0x00000062,  7 }, // 'G' ( 71)  |1100010
  { 0x00000063,  7 }, // 'H' ( 72)  |1100011
  { 0x00000064,  7 }, // 'I' ( 73)  |1100100
  { 0x00000065,  7 }, // 'J' ( 74)  |1100101
  { 0x00000066,  7 }, // 'K' ( 75)  |1100110
  { 0x00000067,  7 }, // 'L' ( 76)  |1100111
  { 0x00000068,  7 }, // 'M' ( 77)  |1101000
  { 0x00000069,  7 }, // 'N' ( 78)  |1101001
  { 0x0000006a,  7 }, // 'O' ( 79)  |1101010
  { 0x0000006b,  7 }, // 'P' ( 80)  |1101011
  { 0x0000006c,  7 }, // 'Q' ( 81)  |1101100
  { 0x0000006d,  7 }, // 'R' ( 82)  |1101101
  { 0x0000006e,  7 }, // 'S' ( 83)  |1101110
  { 0x0000006f,  7 }, // 'T' ( 84)  |1101111
  { 0x00000070,  7 }, // 'U' ( 85)  |1110000
  { 0x00000071,  7 }, // 'V' ( 86)  |1110001
  { 0x00000072,  7 }, // 'W' ( 87)  |1110010
  { 0x000000fc,  8 }, // 'X' ( 88)  |11111100
  { 0x00000073,  7 }, // 'Y' ( 89)  |1110011
  { 0x000000fd,  8 }, // 'Z' ( 90)  |11111101
  { 0x00001ffb, 13 }, // '[' ( 91)  |11111111|11011
  { 0x0007fff0, 19 }, // '\' ( 92)  |11111111|11111110|000
  { 0x00001ffc, 13 }, // ']' ( 93)  |11111111|11100
  { 0x00003ffc, 14 }, // '^' ( 94)  |11111111|111100
  { 0x00000022,  6 }, // '_' ( 95)  |100010
  { 0x00007ffd, 15 }, // '`' ( 96)  |11111111|1111101
  { 0x00000003,  5 }, // 'a' ( 97)  |00011
  { 0x00000023,  6 }, // 'b' ( 98)  |100011
  { 0x00000004,  5 }, // 'c' ( 99)  |00100
  { 0x00000024,  6 }, // 'd' (100)  |100100
  { 0x00000005,  5 }, // 'e' (101)  |00101
  { 0x00000025,  6 }, // 'f' (102)  |100101
  { 0x00000026,  6 }, // 'g' (103)  |100110
  { 0x00000027,  6 }, // 'h' (104)  |100111
  { 0x00000006,  5 }, // 'i' (105)  |00110
  { 0x00000074,  7 }, // 'j' (106)  |1110100
  { 0x00000075,  7 }, // 'k' (107)  |1110101
  { 0x00000028,  6 }, // 'l' (108)  |101000
  { 0x00000029,  6 }, // 'm' (109)  |101001
  { 0x0000002a,  6 }, // 'n' (110)  |101010
  { 0x00000007,  5 }, // 'o' (111)  |00111
  { 0x0000002b,  6 }, // 'p' (112)  |101011
  { 0x00000076,  7 }, // 'q' (113)  |1110110
  { 0x0000002c,  6 }, // 'r' (114)  |101100
  { 0x00000008,  5 }, // 's' (115)  |01000
  { 0x00000009,  5 }, // 't' (116)  |01001
  { 0x0000002d,  6 }, // 'u' (117)  |101101
  { 0x00000077,  7 }, // 'v' (118)  |1110111
  { 0x00000078,  7 }, // 'w' (119)  |1111000
  { 0x00000079,  7 }, // 'x' (120)  |1111001
  { 0x0000007a,  7 }, // 'y' (121)  |1111010
  { 0x0000007b,  7 }, // 'z' (122)  |1111011
  { 0x00007ffe, 15 }, // '{' (123)  |11111111|1111110
  { 0x000007fc, 11 }, // '|' (124)  |11111111|100
  { 0x00003ffd, 14 }, // '}' (125)  |11111111|111101
  { 0x00001ffd, 13 }, // '~' (126)  |11111111|11101
  { 0x0ffffffc, 28 }, //     (127)  |11111111|11111111|11111111|1100
  { 0x000fffe6, 20 }, //     (128)  |11111111|11111110|0110
  { 0x003fffd2, 22 }, //     (129)  |11111111|11111111|010010
  { 0x000fffe7, 20 }, //     (130)  |11111111|11111110|0111
  { 0x000fffe8, 20 }, //     (131)  |11111111|11111110|1000
  { 0x003fffd3, 22 }, //     (132)  |11111111|11111111|010011
  { 0x003fffd4, 22 }, //     (133)  |11111111|11111111|010100
  { 0x003fffd5, 22 }, //     (134)  |11111111|11111111|010101
  { 0x007fffd9, 23 }, //     (135)  |11111111|11111111|1011001
  { 0x003fffd6, 22 }, //     (136)  |11111111|11111111|010110
  { 0x007fffda, 23 }, //     (137)  |11111111|11111111|1011010
  { 0x007fffdb, 23 }, //     (138)  |11111111|11111111|1011011
  { 0x007fffdc, 23 }, //     (139)  |11111111|11111111|1011100
  { 0x007fffdd, 23 }, //     (140)  |11111111|11111111|1011101
  { 0x007fffde, 23 }, //     (141)  |11111111|11111111|1011110
  { 0x00ffffeb, 24 }, //     (142)  |11111111|11111111|11101011
  { 0x007fffdf, 23 }, //     (143)  |11111111|11111111|1011111
  { 0x00ffffec, 24 }, //     (144)  |11111111|11111111|11101100
  { 0x00ffffed, 24 }, //     (145)  |11111111|11111111|11101101
  { 0x003fffd7, 22 }, //     (146)  |11111111|11111111|010111
  { 0x007fffe0, 23 }, //     (147)  |11111111|11111111|1100000
  { 0x00ffffee, 24 }, //     (148)  |11111111|11111111|11101110
  { 0x007fffe1, 23 }, //     (149)  |11111111|11111111|1100001
  { 0x007fffe2, 23 }, //     (150)  |11111111|11111111|1100010
  { 0x007fffe3, 23 }, //     (151)  |11111111|11111111|1100011
  { 0x007fffe4, 23 }, //     (152)  |11111111|11111111|1100100
  { 0x001fffdc, 21 }, //     (153)  |11111111|11111110|11100
  { 0x003fffd8, 22 }, //     (154)  |11111111|11111111|011000
  { 0x007fffe5, 23 }, //     (155)  |11111111|11111111|1100101
  { 0x003fffd9, 22 }, //     (156)  |11111111|11111111|011001
  { 0x007fffe6, 23 }, //     (157)  |11111111|11111111|1100110
  { 0x007fffe7, 23 }, //     (158)  |11111111|11111111|1100111
  { 0x00ffffef, 24 }, //     (159)  |11111111|11111111|11101111
  { 0x003fffda, 22 }, //     (160)  |11111111|11111111|011010
  { 0x001fffdd, 21 }, //     (161)  |11111111|11111110|11101
  { 0x000fffe9, 20 }, //     (162)  |11111111|11111110|1001
  { 0x003fffdb, 22 }, //     (163)  |11111111|11111111|011011
  { 0x003fffdc, 22 }, //     (164)  |11111111|11111111|011100
  { 0x007fffe8, 23 }, //     (165)  |11111111|11111111|1101000
  { 0x007fffe9, 23 }, //     (166)  |11111111|11111111|1101001
  { 0x001fffde, 21 }, //     (167)  |11111111|11111110|11110
  { 0x007fffea, 23 }, //     (168)  |11111111|11111111|1101010
  { 0x003fffdd, 22 }, //     (169)  |11111111|11111111|011101
  { 0x003fffde, 22 }, //     (170)  |11111111|11111111|011110
  { 0x00fffff0, 24 }, //     (171)  |11111111|11111111|11110000
  { 0x001fffdf, 21 }, //     (172)  |11111111|11111110|11111
  { 0x003fffdf, 22 }, //     (173)  |11111111|11111111|011111
  { 0x007fffeb, 23 }, //     (174)  |11111111|11111111|1101011
  { 0x007fffec, 23 }, //     (175)  |11111111|11111111|1101100
  { 0x001fffe0, 21 }, //     (176)  |11111111|11111111|00000
  { 0x001fffe1, 21 }, //     (177)  |11111111|11111111|00001
  { 0x003fffe0, 22 }, //     (178)  |11111111|11111111|100000
  { 0x001fffe2, 21 }, //     (179)  |11111111|11111111|00010
  { 0x007fffed, 23 }, //     (180)  |11111111|11111111|1101101
  { 0x003fffe1, 22 }, //     (181)  |11111111|11111111|100001
  { 0x007fffee, 23 }, //     (182)  |11111111|11111111|1101110
  { 0x007fffef, 23 }, //     (183)  |11111111|11111111|1101111
  { 0x000fffea, 20 }, //     (184)  |11111111|11111110|1010
  { 0x003fffe2, 22 }, //     (185)  |11111111|11111111|100010
  { 0x003fffe3, 22 }, //     (186)  |11111111|11111111|100011
  { 0x003fffe4, 22 }, //     (187)  |11111111|11111111|100100
  { 0x007ffff0, 23 }, //     (188)  |11111111|11111111|1110000
  { 0x003fffe5, 22 }, //     (189)  |11111111|11111111|100101
  { 0x003fffe6, 22 }, //     (190)  |11111111|11111111|100110
  { 0x007ffff1, 23 }, //     (191)  |11111111|11111111|1110001
  { 0x03ffffe0, 26 }, //     (192)  |11111111|11111111|11111000|00
  { 0x03ffffe1, 26 }, //     (193)  |11111111|11111111|11111000|01
  { 0x000fffeb, 20 }, //     (194)  |11111111|11111110|1011
  { 0x0007fff1, 19 }, //     (195)  |11111111|11111110|001
  { 0x003fffe7, 22 }, //     (196)  |11111111|11111111|100111
  { 0x007ffff2, 23 }, //     (197)  |11111111|11111111|1110010
  { 0x003fffe8, 22 }, //     (198)  |11111111|11111111|101000
  { 0x01ffffec, 25 }, //     (199)  |11111111|11111111|11110110|0
  { 0x03ffffe2, 26 }, //     (200)  |11111111|11111111|11111000|10
  { 0x03ffffe3, 26 }, //     (201)  |11111111|11111111|11111000|11
  { 0x03ffffe4, 26 }, //     (202)  |11111111|11111111|11111001|00
  { 0x07ffffde, 27 }, //     (203)  |11111111|11111111|11111011|110
  { 0x07ffffdf, 27 }, //     (204)  |11111111|11111111|11111011|111
  { 0x03ffffe5, 26 }, //     (205)  |11111111|11111111|11111001|01
  { 0x00fffff1, 24 }, //     (206)  |11111111|11111111|11110001
  { 0x01ffffed, 25 }, //     (207)  |11111111|11111111|11110110|1
  { 0x0007fff2, 19 }, //     (208)  |11111111|11111110|010
  { 0x001fffe3, 21 }, //     (209)  |11111111|11111111|00011
  { 0x03ffffe6, 26 }, //     (210)  |11111111|11111111|11111001|10
  { 0x07ffffe0, 27 }, //     (211)  |11111111|11111111|11111100|000
  { 0x07ffffe1, 27 }, //     (212)  |11111111|11111111|11111100|001
  { 0x03ffffe7, 26 }, //     (213)  |11111111|11111111|11111001|11
  { 0x07ffffe2, 27 }, //     (214)  |11111111|11111111|11111100|010
  { 0x00fffff2, 24 }, //     (215)  |11111111|11111111|11110010
  { 0x001fffe4, 21 }, //     (216)  |11111111|11111111|00100
  { 0x001fffe5, 21 }, //     (217)  |11111111|11111111|00101
  { 0x03ffffe8, 26 }, //     (218)  |11111111|11111111|11111010|00
  { 0x03ffffe9, 26 }, //     (219)  |11111111|11111111|11111010|01
  { 0x0ffffffd, 28 }, //     (220)  |11111111|11111111|11111111|1101
  { 0x07ffffe3, 27 }, //     (221)  |11111111|11111111|11111100|011
  { 0x07ffffe4, 27 }, //     (222)  |11111111|11111111|11111100|100
  { 0x07ffffe5, 27 }, //     (223)  |11111111|11111111|11111100|101
  { 0x000fffec, 20 }, //     (224)  |11111111|11111110|1100
  { 0x00fffff3, 24 }, //     (225)  |11111111|11111111|11110011
  { 0x000fffed, 20 }, //     (226)  |11111111|11111110|1101
  { 0x001fffe6, 21 }, //     (227)  |11111111|11111111|00110
  { 0x003fffe9, 22 }, //     (228)  |11111111|11111111|101001
  { 0x001fffe7, 21 }, //     (229)  |11111111|11111111|00111
  { 0x001fffe8, 21 }, //     (230)  |11111111|11111111|01000
  { 0x007ffff3, 23 }, //     (231)  |11111111|11111111|1110011
  { 0x003fffea, 22 }, //     (232)  |11111111|11111111|101010
  { 0x003fffeb, 22 }, //     (233)  |11111111|11111111|101011
  { 0x01ffffee, 25 }, //     (234)  |11111111|11111111|11110111|0
  { 0x01ffffef, 25 }, //     (235)  |11111111|11111111|11110111|1
  { 0x00fffff4, 24 }, //     (236)  |11111111|11111111|11110100
  { 0x00fffff5, 24 }, //     (237)  |11111111|11111111|11110101
  { 0x03ffffea, 26 }, //     (238)  |11111111|11111111|11111010|10
  { 0x007ffff4, 23 }, //     (239)  |11111111|11111111|1110100
  { 0x03ffffeb, 26 }, //     (240)  |11111111|11111111|11111010|11
  { 0x07ffffe6, 27 }, //     (241)  |11111111|11111111|11111100|110
  { 0x03ffffec, 26 }, //     (242)  |11111111|11111111|11111011|00
  { 0x03ffffed, 26 }, //     (243)  |11111111|11111111|11111011|01
  { 0x07ffffe7, 27 }, //     (244)  |11111111|11111111|11111100|111
  { 0x07ffffe8, 27 }, //     (245)  |11111111|11111111|11111101|000
  { 0x07ffffe9, 27 }, //     (246)  |11111111|11111111|11111101|001
  { 0x07ffffea, 27 }, //     (247)  |11111111|11111111|11111101|010
  { 0x07ffffeb, 27 }, //     (248)  |11111111|11111111|11111101|011
  { 0x0ffffffe, 28 }, //     (249)  |11111111|11111111|11111111|1110
  { 0x07ffffec, 27 }, //     (250)  |11111111|11111111|11111101|100
  { 0x07ffffed, 27 }, //     (251)  |11111111|11111111|11111101|101
  { 0x07ffffee, 27 }, //     (252)  |11111111|11111111|11111101|110
  { 0x07ffffef, 27 }, //     (253)  |11111111|11111111|11111101|111
  { 0x07fffff0, 27 }, //     (254)  |11111111|11111111|11111110|000
  { 0x03ffffee, 26 }, //     (255)  |11111111|11111111|11111011|10
  { 0x3fffffff, 30 }, // EOS (256)  |11111111|11111111|11111111|111111
};

//
// Frame
//

auto Frame::decode_window_update(int &increment) -> ErrorCode {
  if (payload.size() != 4) return FRAME_SIZE_ERROR;
  uint8_t buf[4];
  payload.shift(sizeof(buf), buf);
  increment = (
    ((buf[0] & 0x7f) << 24) |
    ((buf[1] & 0xff) << 16) |
    ((buf[2] & 0xff) <<  8) |
    ((buf[3] & 0xff) <<  0)
  );
  return NO_ERROR;
}

void Frame::encode_window_update(int increment) {
  uint8_t buf[4];
  buf[0] = 0x7f & (increment >> 24);
  buf[1] = 0xff & (increment >> 16);
  buf[2] = 0xff & (increment >>  8);
  buf[3] = 0xff & (increment >>  0);
  payload.push(buf, sizeof(buf), &s_dp);
}

//
// FrameDecoder
//

FrameDecoder::FrameDecoder()
  : m_payload(Data::make())
{
  Deframer::reset(STATE_HEADER);
  Deframer::read(sizeof(m_header), m_header);
}

void FrameDecoder::deframe(Data *data) {
  Deframer::deframe(*data);
}

auto FrameDecoder::on_state(int state, int c) -> int {
  switch (state) {
    case STATE_HEADER: {
      const uint8_t *buf = m_header;
      auto size = (
        (uint32_t(buf[0]) << 16) |
        (uint32_t(buf[1]) <<  8) |
        (uint32_t(buf[2]) <<  0)
      );
      m_frame.type = buf[3];
      m_frame.flags = buf[4];
      m_frame.stream_id = (
        (uint32_t(buf[5]) << 24) |
        (uint32_t(buf[6]) << 16) |
        (uint32_t(buf[7]) <<  8) |
        (uint32_t(buf[8]) <<  0)
      ) & 0x7fffffff;
      if (size > m_max_frame_size) {
        on_deframe_error(FRAME_SIZE_ERROR);
        return -1;
      } else if (size > 0) {
        Deframer::read(size, m_payload);
        return STATE_PAYLOAD;
      } else {
        on_deframe(m_frame);
        read(sizeof(m_header), m_header);
        return STATE_HEADER;
      }
    }
    case STATE_PAYLOAD: {
      m_frame.payload = std::move(*m_payload);
      on_deframe(m_frame);
      read(sizeof(m_header), m_header);
      return STATE_HEADER;
    }
  }
  return -1;
}

//
// FrameEncoder
//

void FrameEncoder::frame(Frame &frm, Data &out) {
  uint8_t head[9];
  header(head, frm.stream_id, frm.type, frm.flags, frm.payload.size());
  out.push(head, sizeof(head), &s_dp);
  out.push(frm.payload);
}

void FrameEncoder::RST_STREAM(int id, ErrorCode err, Data &out) {
  uint8_t buf[9 + 4];
  header(buf, id, Frame::RST_STREAM, 0, 4);
  buf[9+0] = 0xff & (err >> 24);
  buf[9+1] = 0xff & (err >> 16);
  buf[9+2] = 0xff & (err >>  8);
  buf[9+3] = 0xff & (err >>  0);
  out.push(buf, sizeof(buf), &s_dp);
}

void FrameEncoder::GOAWAY(int id, ErrorCode err, Data &out) {
  uint8_t buf[9 + 8];
  header(buf, 0, Frame::GOAWAY, 0, 8);
  buf[9+0] = 0xff & (id  >> 24);
  buf[9+1] = 0xff & (id  >> 16);
  buf[9+2] = 0xff & (id  >>  8);
  buf[9+3] = 0xff & (id  >>  0);
  buf[9+4] = 0xff & (err >> 24);
  buf[9+5] = 0xff & (err >> 16);
  buf[9+6] = 0xff & (err >>  8);
  buf[9+7] = 0xff & (err >>  0);
  out.push(buf, sizeof(buf), &s_dp);
}

void FrameEncoder::header(uint8_t *buf, int id, uint8_t type, uint8_t flags, size_t size) {
  buf[0] = 0xff & (size >> 16);
  buf[1] = 0xff & (size >> 8);
  buf[2] = 0xff & (size >> 0);
  buf[3] = type;
  buf[4] = flags;
  buf[5] = 0x7f & (id >> 24);
  buf[6] = 0xff & (id >> 16);
  buf[7] = 0xff & (id >> 8);
  buf[8] = 0xff & (id >> 0);
}

//
// HeaderDecoder
//

HeaderDecoder::StaticTable HeaderDecoder::s_static_table;
HeaderDecoder::HuffmanTree HeaderDecoder::s_huffman_tree;

HeaderDecoder::HeaderDecoder() {
  m_table = pjs::PooledArray<Entry>::make(TABLE_SIZE);
}

void HeaderDecoder::start(bool is_response) {
  if (is_response) {
    m_head = http::ResponseHead::make();
  } else {
    m_head = http::RequestHead::make();
  }
  m_head->headers(pjs::Object::make());
  m_buffer.clear();
  m_state = INDEX_PREFIX;
  m_is_response = is_response;
}

bool HeaderDecoder::decode(Data &data) {
  if (!m_head) return false;

  data.scan(
    [this](int c) -> bool {
      switch (m_state) {
        case ERROR: return false;

        case INDEX_PREFIX: {
          index_prefix(c);
          break;
        }
        case INDEX_OCTETS: {
          if (read_int(c)) index_end();
          break;
        }
        case NAME_PREFIX: {
          name_prefix(c);
          break;
        }
        case NAME_LENGTH: {
          if (read_int(c)) {
            m_ptr = 0;
            m_state = NAME_STRING;
          }
          break;
        }
        case NAME_STRING: {
          if (read_str(c)) {
            m_name = pjs::Str::make(m_buffer.to_string());
            m_buffer.clear();
            m_state = VALUE_PREFIX;
          }
          break;
        }
        case VALUE_PREFIX: {
          value_prefix(c);
          break;
        }
        case VALUE_LENGTH: {
          if (read_int(c)) {
            m_ptr = 0;
            m_state = VALUE_STRING;
          }
          break;
        }
        case VALUE_STRING: {
          if (read_str(c)) {
            auto value = pjs::Str::make(m_buffer.to_string());
            m_buffer.clear();
            add_field(m_name, value);
            if (m_is_new) new_entry(m_name, value);
            m_state = INDEX_PREFIX;
          }
          break;
        }
      }
      return true;
    }
  );
  return m_state != ERROR;
}

void HeaderDecoder::end(pjs::Ref<http::MessageHead> &head) {
  head = m_head;
  m_head = nullptr;
}

bool HeaderDecoder::read_int(uint8_t c) {
  m_int += (c & 0x7f) << m_exp;
  if (c & 0x80) {
    m_exp += 7;
    return false;
  } else {
    return true;
  }
}

bool HeaderDecoder::read_str(uint8_t c) {
  if (m_prefix & 0x80) {
    const auto &tree = s_huffman_tree.get();
    for (int b = 7; b >= 0; b--) {
      bool bit = (c >> b) & 1;
      m_ptr = bit ? tree[m_ptr].right : tree[m_ptr].left;
      auto &node = tree[m_ptr];
      if (!node.left) {
        auto ch = node.right;
        if (ch == 256) break;
        s_dp.push(&m_buffer, char(ch));
        m_ptr = 0;
      }
    }
  } else {
    s_dp.push(&m_buffer, c);
  }
  return !--m_int;
}

void HeaderDecoder::index_prefix(uint8_t prefix) {
  uint8_t mask = 0x0f;
  auto is_new = false;
  if      ((prefix & 0x80) == 0x80) { mask = 0x7f; }
  else if ((prefix & 0xc0) == 0x40) { mask = 0x3f; is_new = true; }
  else if ((prefix & 0xe0) == 0x20) { mask = 0x1f; }
  m_prefix = prefix;
  m_is_new = is_new;
  m_int = prefix & mask;
  if (m_int == mask) {
    m_exp = 0;
    m_state = INDEX_OCTETS;
  } else {
    index_end();
  }
}

void HeaderDecoder::index_end() {
  auto p = m_prefix;
  if ((p & 0x80) == 0x80) {
    if (!m_int) {
      m_state = ERROR;
    } else if (const auto *entry = get_entry(m_int)) {
      auto v = entry->value;
      if (!v) v = pjs::Str::empty;
      add_field(entry->name, v);
      m_state = INDEX_PREFIX;
    } else {
      m_state = ERROR;
    }
  } else if ((p & 0xe0) == 0x20) {
    // TODO: resize dynamic table
    m_state = INDEX_PREFIX;
  } else if (m_int) {
    if (const auto *entry = get_entry(m_int)) {
      m_name = entry->name;
      m_state = VALUE_PREFIX;
    } else {
      m_state = ERROR;
    }
  } else {
    m_state = NAME_PREFIX;
  }
}

void HeaderDecoder::name_prefix(uint8_t prefix) {
  m_prefix = prefix;
  if ((m_int = prefix & 0x7f) == 0x7f) {
    m_exp = 0;
    m_state = NAME_LENGTH;
  } else {
    m_ptr = 0;
    m_state = NAME_STRING;
  }
}

void HeaderDecoder::value_prefix(uint8_t prefix) {
  m_prefix = prefix;
  if ((m_int = prefix & 0x7f) == 0x7f) {
    m_exp = 0;
    m_state = VALUE_LENGTH;
  } else {
    m_ptr = 0;
    m_state = VALUE_STRING;
  }
}

void HeaderDecoder::add_field(pjs::Str *name, pjs::Str *value) {
  if (name == s_colon_method) {
    if (!m_is_response) {
      auto req = m_head->as<http::RequestHead>();
      req->method(value);
    }
  } else if (name == s_colon_scheme) {
    if (!m_is_response) {
      auto req = m_head->as<http::RequestHead>();
      req->scheme(value);
    }
  } else if (name == s_colon_authority) {
    if (!m_is_response) {
      auto req = m_head->as<http::RequestHead>();
      pjs::Value v;
      auto headers = m_head->headers();
      headers->get(s_host, v);
      if (v.is_undefined()) headers->set(s_host, value);
      req->authority(value);
    }
  } else if (name == s_colon_path) {
    if (!m_is_response) {
      auto req = m_head->as<http::RequestHead>();
      req->path(value);
    }
  } else if (name == s_colon_status) {
    if (m_is_response) {
      auto res = m_head->as<http::ResponseHead>();
      res->status(std::atoi(value->c_str()));
    }
  } else {
    auto headers = m_head->headers();
    if (!headers) {
      headers = pjs::Object::make();
      m_head->headers(headers);
    }
    headers->set(name, value);
  }
}

auto HeaderDecoder::get_entry(size_t i) -> const Entry* {
  auto &tab = s_static_table.get();
  if (i <= tab.size()) {
    return &tab[i - 1];
  }
  i -= tab.size() + 1;
  if (i < m_table_tail - m_table_head) {
    return &m_table->at((m_table_head + i) % TABLE_SIZE);
  }
  return nullptr;
}

void HeaderDecoder::new_entry(pjs::Str *name, pjs::Str *value) {
  if (--m_table_head == m_table_tail) {
    m_table_tail--;
  }
  auto &ent = m_table->at(m_table_head % TABLE_SIZE);
  ent.name = name;
  ent.value = value;
}

//
// HeaderDecoder::StaticTable
//

HeaderDecoder::StaticTable::StaticTable() {
  int n = sizeof(s_hpack_static_table) / sizeof(s_hpack_static_table[0]);
  for (int i = 0; i < n; i++) {
    m_table.emplace_back();
    auto &ent = m_table.back();
    auto &p = s_hpack_static_table[i];
    ent.name = pjs::Str::make(p.name);
    ent.value = p.value ? pjs::Str::make(p.value) : pjs::Str::empty.get();
  }
}

//
// HeaderDecoder::HuffmanTree
//

HeaderDecoder::HuffmanTree::HuffmanTree() {
  int n = sizeof(s_hpack_huffman_table) / sizeof(s_hpack_huffman_table[0]);
  m_tree.resize(1);
  for (int i = 0; i < n; i++) {
    auto &p = s_hpack_huffman_table[i];
    int ptr = 0;
    for (int b = p.bits - 1; b >= 0; b--) {
      bool bit = (p.code >> b) & 1;
      auto &node = m_tree[ptr];
      ptr = bit ? node.right : node.left;
      if (!ptr) {
        ptr = m_tree.size();
        (bit ? node.right : node.left) = ptr;
        m_tree.emplace_back();
      }
    }
    m_tree[ptr].right = i;
  }
}

//
// HeaderEncoder
//

HeaderEncoder::StaticTable HeaderEncoder::m_static_table;

void HeaderEncoder::encode(bool is_response, pjs::Object *head, Data &data) {
  bool has_authority = false;
  if (is_response) {
    pjs::Value status;
    if (head) head->get(s_status, status);
    if (status.is_number()) {
      pjs::Ref<pjs::Str> str(pjs::Str::make(status.n()));
      encode_header_field(data, s_colon_status, str);
    } else {
      encode_header_field(data, s_colon_status, s_200);
    }

  } else {
    pjs::Value method, scheme, authority, path;
    if (head) {
      head->get(s_method, method);
      head->get(s_scheme, scheme);
      head->get(s_authority, authority);
      head->get(s_path, path);
    }
    if (method.is_string()) {
      encode_header_field(data, s_colon_method, method.s());
    } else {
      encode_header_field(data, s_colon_method, s_GET);
    }
    if (scheme.is_string()) {
      encode_header_field(data, s_colon_scheme, scheme.s());
    } else {
      encode_header_field(data, s_colon_scheme, s_http);
    }
    if (authority.is_string()) {
      encode_header_field(data, s_colon_authority, authority.s());
      has_authority = true;
    }
    if (path.is_string()) {
      encode_header_field(data, s_colon_path, path.s());
    } else {
      encode_header_field(data, s_colon_path, s_root_path);
    }
  }

  pjs::Value headers;
  if (head) head->get(s_headers, headers);
  if (headers.is_object()) {
    if (auto obj = headers.o()) {
      obj->iterate_all(
        [&](pjs::Str *k, pjs::Value &v) {
          if (k == s_host) {
            if (has_authority) return;
            k = s_colon_authority;
          }
          if (k == s_connection) return;
          if (k == s_keep_alive) return;
          auto s = v.to_string();
          encode_header_field(data, k, s);
          s->release();
        }
      );
    }
  }
}

void HeaderEncoder::encode_header_field(Data &data, pjs::Str *k, pjs::Str *v) {
  if (const auto *ent = m_static_table.find(k)) {
    auto i = ent->values.find(v);
    if (i == ent->values.end()) {
      encode_int(data, 0x00, 4, ent->index);
      encode_str(data, v);
    } else {
      encode_int(data, 0x80, 1, i->second);
    }
  } else {
    encode_int(data, 0x00, 4, 0);
    encode_str(data, k);
    encode_str(data, v);
  }
}

void HeaderEncoder::encode_int(Data &data, uint8_t prefix, int prefix_len, uint32_t n) {
  uint8_t mask = (1 << (8 - prefix_len)) - 1;
  if (n < mask) {
    s_dp.push(&data, prefix | n);
  } else {
    s_dp.push(&data, prefix | mask);
    n -= mask;
    while (n) {
      if (n >> 7) {
        s_dp.push(&data, 0x80 | (n & 0x7f));
      } else {
        s_dp.push(&data, n & 0x7f);
      }
      n >>= 7;
    }
  }
}

void HeaderEncoder::encode_str(Data &data, pjs::Str *s) {
  encode_int(data, 0, 1, s->size());
  s_dp.push(&data, s->str());
}

HeaderEncoder::StaticTable::StaticTable() {
  int n = sizeof(s_hpack_static_table) / sizeof(s_hpack_static_table[0]);
  for (int i = 0; i < n; i++) {
    const auto &f = s_hpack_static_table[i];
    const auto name = pjs::Str::make(f.name);
    auto &ent = m_table[name];
    if (!ent.index) ent.index = i + 1;
    if (f.value) ent.values[pjs::Str::make(f.value)] = i + 1;
  }
}

auto HeaderEncoder::StaticTable::find(pjs::Str *name) -> const Entry* {
  auto i = m_table.find(name);
  if (i == m_table.end()) return nullptr;
  return &i->second;
}

//
// Settings
//

auto Settings::decode(const uint8_t *data, int size) -> ErrorCode {
  for (int i = 0; i + 6 <= size; i += 6) {
    uint16_t k = ((uint16_t)data[i+0] <<  8)|((uint16_t)data[i+1] <<  0);
    uint32_t v = ((uint32_t)data[i+2] << 24)|((uint32_t)data[i+3] << 16)|
                 ((uint32_t)data[i+4] <<  8)|((uint32_t)data[i+5] <<  0);
    switch (k) {
      case 0x1:
        header_table_size = v;
        break;
      case 0x2:
        if (0xfffffffe & v) return PROTOCOL_ERROR;
        enable_push = v;
        break;
      case 0x3:
        max_concurrent_streams = v;
        break;
      case 0x4:
        if (v > 0x7fffffff) return FLOW_CONTROL_ERROR;
        initial_window_size = v;
        break;
      case 0x5:
        if (v < 0x4000 || v > 0xffffff) return PROTOCOL_ERROR;
        max_frame_size = v;
        break;
      case 0x6:
        max_header_list_size = v;
        break;
      default: break;
    }
  }
  return NO_ERROR;
}

auto Settings::encode(uint8_t *data) const -> int {
  int p = 0;
  auto write = [&](int k, int v) {
    data[p+0] = 0xff & (k >>  8);
    data[p+1] = 0xff & (k >>  0);
    data[p+2] = 0xff & (v >> 24);
    data[p+3] = 0xff & (v >> 16);
    data[p+4] = 0xff & (v >>  8);
    data[p+5] = 0xff & (v >>  0);
    p += 6;
  };

  write(0x1, header_table_size);
  write(0x2, enable_push ? 1 : 0);
  if (max_concurrent_streams >= 0) write(0x3, max_concurrent_streams);
  write(0x4, initial_window_size);
  write(0x5, max_frame_size);
  if (max_header_list_size >= 0) write(0x6, max_header_list_size);

  return p;
}

//
// StreamBase
//

static const int MAX_HEADER_FRAME_SIZE = 1024;

int StreamBase::m_server_stream_count = 0;
int StreamBase::m_client_stream_count = 0;

StreamBase::StreamBase(
  int id,
  bool is_server_side,
  HeaderDecoder &header_decoder,
  HeaderEncoder &header_encoder,
  const Settings &settings
) : m_id(id)
  , m_is_server_side(is_server_side)
  , m_header_decoder(header_decoder)
  , m_header_encoder(header_encoder)
  , m_settings(settings)
{
  if (is_server_side) {
    m_server_stream_count++;
  } else {
    m_client_stream_count++;
  }
}

StreamBase::~StreamBase() {
  if (m_is_server_side) {
    m_server_stream_count--;
  } else {
    m_client_stream_count--;
  }
}

bool StreamBase::update_send_window(int delta) {
  if (delta > 0 && m_send_window > 0) {
    auto n = (uint32_t)m_send_window + (uint32_t)delta;
    if (n > 0x7fffffff) {
      connection_error(FLOW_CONTROL_ERROR);
      return false;
    }
  }
  m_send_window += delta;
  pump();
  return true;
}

void StreamBase::on_frame(Frame &frm) {
  switch (frm.type) {
    case Frame::DATA: {
      if (m_header_decoder.started()) {
        stream_error(PROTOCOL_ERROR);
      } else if (m_state == OPEN || m_state == HALF_CLOSED_LOCAL) {
        if (frm.is_PADDED() && !parse_padding(frm)) break;
        if (frm.payload.size()) {
          auto size = frm.payload.size();
          if (size > m_recv_window) {
            connection_error(FLOW_CONTROL_ERROR);
            break;
          }
          if (!deduct_recv(size)) break;
          m_recv_window -= size;
          if (m_recv_window <= INITIAL_RECV_WINDOW_SIZE / 2) {
            Frame frm;
            frm.stream_id = m_id;
            frm.type = Frame::WINDOW_UPDATE;
            frm.flags = 0;
            frm.encode_window_update(INITIAL_RECV_WINDOW_SIZE - m_recv_window);
            frame(frm);
            m_recv_window = INITIAL_RECV_WINDOW_SIZE;
          }
          event(Data::make(frm.payload));
        }
        if (frm.is_END_STREAM()) {
          if (m_state == OPEN) {
            m_state = HALF_CLOSED_REMOTE;
            stream_end();
          } else if (m_state == HALF_CLOSED_LOCAL) {
            m_state = CLOSED;
            stream_end();
            close();
          }
        } else if (m_is_tunnel) {
          event(Data::flush());
        }
      } else {
        stream_error(STREAM_CLOSED);
      }
      break;
    }

    case Frame::HEADERS: {
      if (frm.is_PADDED() && !parse_padding(frm)) break;
      if (frm.is_PRIORITY() && !parse_priority(frm)) break;
      if (frm.is_END_STREAM()) m_end_input = true;
      m_header_decoder.start(!m_is_server_side);
      parse_headers(frm);
      break;
    }

    case Frame::PRIORITY: {
      if (frm.payload.size() != 5) {
        stream_error(FRAME_SIZE_ERROR);
      } else {
        parse_priority(frm);
      }
      break;
    }

    case Frame::RST_STREAM: {
      if (m_state == IDLE) {
        connection_error(PROTOCOL_ERROR);
      } else if (frm.payload.size() != 4) {
        connection_error(FRAME_SIZE_ERROR);
      } else {
        m_state = CLOSED;
        event(StreamEnd::make(StreamEnd::CONNECTION_RESET));
        close();
      }
      break;
    }

    case Frame::PUSH_PROMISE: {
      // TODO
      break;
    }

    case Frame::WINDOW_UPDATE: {
      auto inc = 0;
      auto err = frm.decode_window_update(inc);
      if (err == NO_ERROR) {
        update_send_window(inc);
      } else {
        connection_error(err);
      }
      break;
    }

    case Frame::CONTINUATION: {
      parse_headers(frm);
      break;
    }
  }
}

void StreamBase::on_event(Event *evt) {
  if (auto start = evt->as<MessageStart>()) {
    Data buf;
    m_header_encoder.encode(m_is_server_side, start->head(), buf);
    while (!buf.empty()) {
      auto len = std::min(buf.size(), MAX_HEADER_FRAME_SIZE);
      Frame frm;
      frm.stream_id = m_id;
      frm.type = Frame::HEADERS;
      frm.flags = (len == buf.size() ? Frame::BIT_END_HEADERS : 0);
      buf.shift(len, frm.payload);
      frame(frm);
    }
    if (m_state == IDLE) {
      m_state = OPEN;
    } else if (m_state == RESERVED_LOCAL) {
      m_state = HALF_CLOSED_REMOTE;
    }
    if (!m_is_server_side) {
      if (auto head = start->head()) {
        pjs::Value method;
        head->get(s_method, method);
        if (method.is_string() && method.s() == s_CONNECT) {
          m_is_tunnel = true;
        }
      }
    }

  } else if (auto data = evt->as<Data>()) {
    if (data->empty()) {
      flush();
    } else if (m_is_tunnel) {
      m_send_buffer.push(*data);
      pump();
    } else if (!m_end_output) {
      pump();
      m_send_buffer.push(*data);
    }

  } else if (
    (evt->is<MessageEnd>() && !m_is_tunnel) ||
    (evt->is<StreamEnd>()))
  {
    if (m_state == OPEN) {
      m_state = HALF_CLOSED_LOCAL;
      m_end_output = true;
      pump();
    } else if (m_state == HALF_CLOSED_REMOTE) {
      m_state = CLOSED;
      m_end_output = true;
      pump();
    }
    recycle();
  }
}

void StreamBase::on_pump() {
  pump();
  recycle();
}

bool StreamBase::parse_padding(Frame &frm) {
  uint8_t pad_length = 0;
  frm.payload.shift(1, &pad_length);
  if (pad_length >= frm.payload.size()) {
    connection_error(PROTOCOL_ERROR);
    return false;
  }
  frm.payload.pop(pad_length);
  return true;
}

bool StreamBase::parse_priority(Frame &frm) {
  if (frm.payload.size() < 5) {
    connection_error(PROTOCOL_ERROR);
    return false;
  }
  uint8_t buf[5];
  frm.payload.shift(sizeof(buf), buf);
  return true;
}

void StreamBase::parse_headers(Frame &frm) {
  if (!m_header_decoder.decode(frm.payload)) {
    connection_error(COMPRESSION_ERROR);
    return;
  }

  if (frm.is_END_HEADERS()) {
    pjs::Ref<http::MessageHead> head;
    m_header_decoder.end(head);

    if (m_state == IDLE) {
      m_state = OPEN;
    } else if (m_state == RESERVED_REMOTE) {
      m_state = HALF_CLOSED_LOCAL;
    }

    event(MessageStart::make(head));

    if (m_is_server_side) {
      if (head->as<http::RequestHead>()->method() == s_CONNECT) {
        m_is_tunnel = true;
        event(MessageEnd::make());
      }
    } else if (m_is_tunnel) {
      event(MessageEnd::make());
    }

    if (m_end_input) {
      if (m_state == OPEN) {
        m_state = HALF_CLOSED_REMOTE;
        stream_end();
      } else if (m_state == HALF_CLOSED_LOCAL) {
        m_state = CLOSED;
        stream_end();
        close();
      }
    }
  }
}

void StreamBase::pump() {
  bool is_empty_end = (m_end_output && m_send_buffer.empty());
  int size = m_send_buffer.size();
  if (size > m_send_window) size = m_send_window;
  if (size > 0) size = deduct_send(size);
  if (size > 0 || is_empty_end) {
    auto remain = size;
    do {
      auto n = std::min(remain, m_settings.max_frame_size);
      remain -= n;
      Frame frm;
      frm.stream_id = m_id;
      frm.type = Frame::DATA;
      if (n > 0) m_send_buffer.shift(n, frm.payload);
      if (m_end_output && m_send_buffer.empty()) {
        frm.flags = Frame::BIT_END_STREAM;
        m_end_output = false;
      } else {
        frm.flags = 0;
      }
      frame(frm);
    } while (remain > 0);
    m_send_window -= size;
  }
}

void StreamBase::recycle() {
  flush();
  if (m_state == CLOSED && m_send_buffer.empty()) {
    close();
  }
}

void StreamBase::stream_end() {
  if (m_is_tunnel) {
    event(StreamEnd::make());
  } else {
    event(MessageEnd::make());
  }
}

//
// Demuxer
//

Demuxer::Demuxer() {
  m_settings.enable_push = false;
}

Demuxer::~Demuxer() {
  for (const auto &p : m_streams) {
    delete p.second;
  }
  delete m_initial_stream;
}

auto Demuxer::initial_stream() -> Input* {
  if (!m_initial_stream) {
    m_initial_stream = new InitialStream(this);
  }
  return m_initial_stream->input();
}

void Demuxer::start() {
  if (m_initial_stream) {
    m_initial_stream->start();
    delete m_initial_stream;
    m_initial_stream = nullptr;
  }
}

void Demuxer::shutdown() {
  // TODO
}

void Demuxer::stream_close(int id) {
  auto i = m_streams.find(id);
  if (i != m_streams.end()) {
    delete i->second;
    m_streams.erase(i);
  }
}

void Demuxer::stream_error(int id, ErrorCode err) {
  stream_close(id);
  FrameEncoder::RST_STREAM(id, err, m_output_buffer);
}

void Demuxer::connection_error(ErrorCode err) {
  for (const auto &p : m_streams) {
    delete p.second;
  }
  m_streams.clear();
  m_has_gone_away = true;
  FrameEncoder::GOAWAY(m_last_received_stream_id, err, m_output_buffer);
  EventFunction::output()->input(Data::make(m_output_buffer));
  EventFunction::output()->input(StreamEnd::make());
  m_output_buffer.clear();
}

void Demuxer::on_event(Event *evt) {
  if (m_has_gone_away) return;

  if (auto data = evt->as<Data>()) {
    if (!data->empty()) {
      m_processing_frames = true;
      FrameDecoder::deframe(data);
      m_processing_frames = false;
    }
    flush();

  } else if (evt->is<StreamEnd>()) {
    std::map<int, Stream*> streams(std::move(m_streams));
    for (auto &p : streams) {
      auto s = p.second;
      s->event(evt->clone());
      delete s;
    }
  }
}

void Demuxer::on_deframe(Frame &frm) {
  if (auto id = frm.stream_id) {
    if (
      frm.type == Frame::SETTINGS ||
      frm.type == Frame::PING ||
      frm.type == Frame::GOAWAY
    ) {
      connection_error(PROTOCOL_ERROR);
    } else {
      Stream *stream = nullptr;
      auto i = m_streams.find(id);
      if (i == m_streams.end()) {
        if ((id & 1) == 0) {
          connection_error(PROTOCOL_ERROR);
          return;
        }
        if (id <= m_last_received_stream_id) {
          // closed stream, ignore
          return;
        }
        stream = new Stream(this, id);
        m_streams[id] = stream;
        m_last_received_stream_id = id;
      } else {
        stream = i->second;
      }
      stream->on_frame(frm);
    }
  } else {
    switch (frm.type) {
      case Frame::SETTINGS: {
        if (!frm.is_ACK()) {
          auto len = frm.payload.size();
          if (len <= Settings::MAX_SIZE) {
            uint8_t buf[len];
            frm.payload.to_bytes(buf);
            auto old_initial_window_size = m_settings.initial_window_size;
            auto err = m_settings.decode(buf, len);
            if (err == NO_ERROR) {
              bool ok = true;
              if (m_settings.initial_window_size != old_initial_window_size) {
                auto delta = m_settings.initial_window_size - old_initial_window_size;
                for (const auto &p : m_streams) {
                  if (!p.second->update_send_window(delta)) {
                    ok = false;
                    break;
                  }
                }
              }
              if (ok) {
                Frame frm;
                frm.stream_id = 0;
                frm.type = Frame::SETTINGS;
                frm.flags = Frame::BIT_ACK;
                frame(frm);
              }
            } else {
              connection_error(err);
            }
          }
        }
        break;
      }
      case Frame::PING: {
        if (frm.payload.size() != 8) {
          connection_error(FRAME_SIZE_ERROR);
        } else if (!frm.is_ACK()) {
          frm.flags |= Frame::BIT_ACK;
          frame(frm);
        }
        break;
      }
      case Frame::GOAWAY: {
        connection_error(NO_ERROR);
        break;
      }
      case Frame::WINDOW_UPDATE: {
        auto inc = 0;
        auto err = frm.decode_window_update(inc);
        if (err == NO_ERROR) {
          auto n = (uint32_t)m_send_window + (uint32_t)inc;
          if (n > 0x7fffffff) {
            connection_error(FLOW_CONTROL_ERROR);
          } else {
            m_send_window = n;
            auto i = m_streams.begin();
            while (i != m_streams.end()) {
              auto *s = i->second;
              i++;
              s->on_pump();
              if (!m_send_window) break;
            }
          }
        } else {
          connection_error(err);
        }
        break;
      }
      default: break;
    }
  }
}

void Demuxer::on_deframe_error(ErrorCode err) {
  connection_error(err);
}

void Demuxer::frame(Frame &frm) {
  if (!m_has_sent_preface) {
    m_has_sent_preface = true;
    uint8_t buf[Settings::MAX_SIZE];
    auto len = m_settings.encode(buf);
    Frame frm;
    frm.stream_id = 0;
    frm.type = Frame::SETTINGS;
    frm.flags = 0;
    frm.payload.push(buf, len, &s_dp);
    FrameEncoder::frame(frm, m_output_buffer);
  }
  FrameEncoder::frame(frm, m_output_buffer);
}

void Demuxer::flush() {
  if (!m_output_buffer.empty() && !m_processing_frames) {
    output(Data::make(m_output_buffer));
    output(Data::flush());
    m_output_buffer.clear();
  }
}

//
// Demuxer::Stream
//

Demuxer::Stream::Stream(Demuxer *demuxer, int id)
  : StreamBase(id, true, demuxer->m_header_decoder, demuxer->m_header_encoder, demuxer->m_peer_settings)
  , m_demuxer(demuxer)
{
  auto p = demuxer->on_new_sub_pipeline();
  EventSource::chain(p->input());
  p->chain(EventSource::reply());
  m_pipeline = p;
}

auto Demuxer::Stream::deduct_send(int size) -> int {
  if (size > m_demuxer->m_send_window) {
    size = m_demuxer->m_send_window;
  }
  m_demuxer->m_send_window -= size;
  return size;
}

bool Demuxer::Stream::deduct_recv(int size) {
  auto &win_size = m_demuxer->m_recv_window;
  if (size > win_size) {
    connection_error(FLOW_CONTROL_ERROR);
    return false;
  }
  win_size -= size;
  if (win_size <= INITIAL_RECV_WINDOW_SIZE / 2) {
    Frame frm;
    frm.stream_id = 0;
    frm.type = Frame::WINDOW_UPDATE;
    frm.flags = 0;
    frm.encode_window_update(INITIAL_RECV_WINDOW_SIZE - win_size);
    frame(frm);
    win_size = INITIAL_RECV_WINDOW_SIZE;
  }
  return true;
}

//
// Demuxer::InitialStream
//

void Demuxer::InitialStream::start() {
  auto *s = new Stream(m_demuxer, 1);
  m_demuxer->m_streams[1] = s;
  if (m_head) {
    if (auto headers = m_head->headers()) {
      pjs::Value settings;
      headers->get(s_http2_settings, settings);
      if (settings.is_string()) {
        const auto &b64 = settings.s()->str();
        const auto size = b64.size() / 4 * 3 + 3;
        if (size < Settings::MAX_SIZE) {
          uint8_t buf[size];
          auto len = utils::decode_base64url(buf, b64.c_str(), b64.length());
          m_demuxer->m_peer_settings.decode(buf, len);
        }
      }
    }
    s->event(MessageStart::make(m_head));
    if (!m_body.empty()) s->event(Data::make(m_body));
    s->event(MessageEnd::make());
  }
}

void Demuxer::InitialStream::on_event(Event *evt) {
  if (auto *start = evt->as<MessageStart>()) {
    if (!m_started) {
      m_head = start->head()->as<http::RequestHead>();
      m_body.clear();
      m_started = true;
    }
  } else if (auto *data = evt->as<Data>()) {
    if (m_started) {
      m_body.push(*data);
    }
  } else if (evt->is<MessageEnd>()) {
    m_started = false;
  }
}

//
// Muxer
//

void Muxer::open(EventFunction *session) {
  EventSource::chain(session->input());
  session->chain(EventSource::reply());
}

auto Muxer::stream() -> EventFunction* {
  auto id = (m_last_sent_stream_id += 2);
  auto stream = new Stream(this, id);
  m_streams[id] = stream;
  return stream;
}

void Muxer::close(EventFunction *stream) {
  auto *s = static_cast<Stream*>(stream);
  stream_close(s->id());
  delete s;
}

void Muxer::close() {
  connection_error(NO_ERROR);
}

void Muxer::stream_close(int id) {
  m_streams.erase(id);
}

void Muxer::stream_error(int id, ErrorCode err) {
  stream_close(id);
  FrameEncoder::RST_STREAM(id, err, m_output_buffer);
}

void Muxer::connection_error(ErrorCode err) {
  m_streams.clear();
  m_has_gone_away = true;
  FrameEncoder::GOAWAY(0, err, m_output_buffer);
  EventSource::output()->input(Data::make(m_output_buffer));
  EventSource::output()->input(StreamEnd::make());
  m_output_buffer.clear();
}

void Muxer::frame(Frame &frm) {
  if (m_has_gone_away) return;
  if (!m_has_sent_preface) {
    m_has_sent_preface = true;
    static Data s_preface("PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n", &s_dp);
    EventSource::output(Data::make(s_preface));
  }
  FrameEncoder::frame(frm, m_output_buffer);
}

void Muxer::flush() {
  if (!m_output_buffer.empty()) {
    EventSource::output(Data::make(m_output_buffer));
    EventSource::output(Data::flush());
    m_output_buffer.clear();
  }
}

void Muxer::on_event(Event *evt) {
  if (auto data = evt->as<Data>()) {
    deframe(data);

  } else if (evt->is<StreamEnd>()) {
    std::map<int, Stream*> streams(std::move(m_streams));
    for (const auto &p : streams) {
      auto s = p.second;
      s->event(evt->clone());
    }
  }
}

void Muxer::on_deframe(Frame &frm) {
  if (auto id = frm.stream_id) {
    if (
      frm.type == Frame::SETTINGS ||
      frm.type == Frame::PING ||
      frm.type == Frame::GOAWAY
    ) {
      connection_error(PROTOCOL_ERROR);
    } else {
      auto i = m_streams.find(id);
      if (i == m_streams.end()) {
        // closed stream, ignore
        return;
      }
      i->second->on_frame(frm);
    }

  } else {
    switch (frm.type) {
      case Frame::SETTINGS: {
        // TODO
        if (!frm.is_ACK()) {
          Frame frm;
          frm.stream_id = 0;
          frm.type = Frame::SETTINGS;
          frm.flags = Frame::BIT_ACK;
          frame(frm);
        }
        break;
      }
      case Frame::PING: {
        if (frm.payload.size() != 8) {
          connection_error(FRAME_SIZE_ERROR);
        } else if (!frm.is_ACK()) {
          frm.flags |= Frame::BIT_ACK;
          frame(frm);
        }
        break;
      }
      case Frame::GOAWAY: {
        connection_error(NO_ERROR);
        break;
      }
      case Frame::WINDOW_UPDATE: {
        // TODO
        break;
      }
      default: break;
    }
  }
}

void Muxer::on_deframe_error(ErrorCode err) {
  connection_error(err);
}

//
// Muxer::Stream
//

Muxer::Stream::Stream(Muxer *muxer, int id)
  : StreamBase(id, false, muxer->m_header_decoder, muxer->m_header_encoder, muxer->m_peer_settings)
  , m_muxer(muxer)
{
}

} // namespace http2
} // namespace pipy
