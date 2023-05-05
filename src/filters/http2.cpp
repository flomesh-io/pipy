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
#include "api/stats.hpp"
#include "log.hpp"

#define DEBUG_HTTP2 1

#if DEBUG_HTTP2
#include <iomanip>
#endif

//
// HTTP/2 Frame Format
//
// +-----------------------------------------------+
// |                 Length (24)                   |
// +---------------+---------------+---------------+
// |   Type (8)    |   Flags (8)   |
// +-+-------------+---------------+-------------------------------+
// |R|                 Stream Identifier (31)                      |
// +=+=============================================================+
// |                   Frame Payload (0...)                      ...
// +---------------------------------------------------------------+
//

namespace pipy {
namespace http2 {

thread_local static Data::Producer s_dp("HTTP/2");

//
// HPACK static table
//

thread_local static const pjs::ConstStr s_colon_scheme(":scheme");
thread_local static const pjs::ConstStr s_colon_method(":method");
thread_local static const pjs::ConstStr s_colon_path(":path");
thread_local static const pjs::ConstStr s_colon_status(":status");
thread_local static const pjs::ConstStr s_colon_authority(":authority");
thread_local static const pjs::ConstStr s_method("method");
thread_local static const pjs::ConstStr s_scheme("scheme");
thread_local static const pjs::ConstStr s_authority("authority");
thread_local static const pjs::ConstStr s_host("host");
thread_local static const pjs::ConstStr s_path("path");
thread_local static const pjs::ConstStr s_status("status");
thread_local static const pjs::ConstStr s_headers("headers");
thread_local static const pjs::ConstStr s_http("http");
thread_local static const pjs::ConstStr s_GET("GET");
thread_local static const pjs::ConstStr s_CONNECT("CONNECT");
thread_local static const pjs::ConstStr s_root_path("/");
thread_local static const pjs::ConstStr s_200("200");
thread_local static const pjs::ConstStr s_http2_settings("http2-settings");
thread_local static const pjs::ConstStr s_connection("connection");
thread_local static const pjs::ConstStr s_keep_alive("keep-alive");
thread_local static const pjs::ConstStr s_proxy_connection("proxy-connection");
thread_local static const pjs::ConstStr s_transfer_encoding("transfer-encoding");
thread_local static const pjs::ConstStr s_upgrade("upgrade");
thread_local static const pjs::ConstStr s_te("te");
thread_local static const pjs::ConstStr s_trailers("trailers");
thread_local static const pjs::ConstStr s_content_length("content-length");

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
// Frame
//

auto Frame::decode_window_update(int &increment) const -> ErrorCode {
  if (payload.size() != 4) return FRAME_SIZE_ERROR;
  uint8_t buf[4];
  payload.to_bytes(buf);
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

void Frame::debug_dump(std::ostream &out) const {
#if DEBUG_HTTP2
  switch (type) {
    case DATA:          out << "DATA         "; break;
    case HEADERS:       out << "HEADERS      "; break;
    case PRIORITY:      out << "PRIORITY     "; break;
    case RST_STREAM:    out << "RST_STREAM   "; break;
    case SETTINGS:      out << "SETTINGS     "; break;
    case PUSH_PROMISE:  out << "PUSH_PROMISE "; break;
    case PING:          out << "PING         "; break;
    case GOAWAY:        out << "GOAWAY       "; break;
    case WINDOW_UPDATE: out << "WINDOW_UPDATE"; break;
    case CONTINUATION:  out << "CONTINUATION "; break;
  }
  out << std::left << " stream " << std::setw(3) << stream_id;
  if (type == SETTINGS || type == PING) {
    out << " ack " << is_ACK();
  } else {
    out << " eos " << is_END_STREAM();
  }
  out << " eoh " << is_END_HEADERS();
  out << " pad " << is_PADDED();
  out << " pri " << is_PRIORITY();
  switch (type) {
    case DATA: {
      out << " dat_siz " << payload.size();
      break;
    }
    case SETTINGS: {
      if (!is_ACK()) {
        Settings settings;
        int len = payload.size();
        uint8_t buf[len];
        payload.to_bytes(buf);
        settings.decode(buf, len);
        out << " enb_psh " << settings.enable_push;
        out << " max_stm " << std::setw(3) << settings.max_concurrent_streams;
        out << " max_frm " << std::setw(5) << settings.max_frame_size;
        out << " max_hdr " << std::setw(5) << settings.max_header_list_size;
        out << " tab_siz " << std::setw(5) << settings.header_table_size;
        out << " win_siz " << std::setw(8) << settings.initial_window_size;
        out << std::setw(0);
      }
      break;
    }
    case WINDOW_UPDATE: {
      int inc = -1;
      decode_window_update(inc);
      out << " win_inc " << inc;
      break;
    }
    default: break;
  }
#endif // DEBUG_HTTP2
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
        if (
          (m_frame.type == Frame::RST_STREAM && size != 4) ||
          (m_frame.type == Frame::PRIORITY && size != 5)
        ) {
          on_deframe_error(FRAME_SIZE_ERROR);
          return -1;
        }
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
      m_frame.payload.clear();
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
// DynamicTable
//

auto DynamicTable::get(size_t i) const -> const TableEntry* {
  auto n = m_head - m_tail;
  if (i >= n) return nullptr;
  return m_entries[(m_head - i) % MAX_ENTRY_COUNT];
}

void DynamicTable::add(pjs::Str *name, pjs::Str *value) {
  auto i = ++m_head;
  if (i - m_tail >= MAX_ENTRY_COUNT) {
    auto entry = m_entries[++m_tail % MAX_ENTRY_COUNT];
    m_size -= 32 + entry->name->size() + entry->value->size();
    delete entry;
  }
  auto entry = m_entries[i % MAX_ENTRY_COUNT] = new TableEntry;
  entry->name = name;
  entry->value = value;
  m_size += 32 + name->size() + value->size();
  evict();
}

void DynamicTable::evict() {
  while (m_size > m_capacity) {
    auto i = ++m_tail;
    auto entry = m_entries[i % MAX_ENTRY_COUNT];
    m_size -= 32 + entry->name->size() + entry->value->size();
    delete entry;
  }
}

//
// HeaderDecoder
//

thread_local
const HeaderDecoder::StaticTable HeaderDecoder::s_static_table;
const HeaderDecoder::HuffmanTree HeaderDecoder::s_huffman_tree;

HeaderDecoder::HeaderDecoder(const Settings &settings)
  : m_settings(settings)
{
}

void HeaderDecoder::start(bool is_response, bool is_trailer) {
  if (is_response) {
    m_head = http::ResponseHead::make();
  } else {
    m_head = http::RequestHead::make();
  }
  m_head->headers = pjs::Object::make();
  m_buffer.clear();
  m_state = INDEX_PREFIX;
  m_is_response = is_response;
  m_is_trailer = is_trailer;
  m_is_pseudo_end = false;
  if (!is_trailer) m_content_length = -1;
}

auto HeaderDecoder::decode(Data &data) -> ErrorCode {
  if (!m_head) return INTERNAL_ERROR;

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
          if (!m_int) {
            error();
            return false;
          }
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
          if (read_str(c, true)) {
            m_name = pjs::Str::make(m_buffer.to_string());
            m_buffer.clear();
            m_state = VALUE_PREFIX;
          }
          break;
        }
        case VALUE_PREFIX: {
          value_prefix(c);
          if (!m_int) {
            if (add_field(m_name, pjs::Str::empty)) {
              if (m_is_new) new_entry(m_name, pjs::Str::empty);
              m_state = INDEX_PREFIX;
            }
          }
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
          if (read_str(c, false)) {
            auto value = pjs::Str::make(m_buffer.to_string());
            m_buffer.clear();
            if (add_field(m_name, value)) {
              if (m_is_new) new_entry(m_name, value);
              m_state = INDEX_PREFIX;
            }
          }
          break;
        }
      }
      return true;
    }
  );
  return m_state == ERROR ? m_error : NO_ERROR;
}

auto HeaderDecoder::end(pjs::Ref<http::MessageHead> &head) -> ErrorCode {
  head = m_head; m_head = nullptr;
  if (m_state != INDEX_PREFIX) return COMPRESSION_ERROR; // incomplete header block
  if ((m_entry_prefix & 0xe0) == 0x20) return COMPRESSION_ERROR; // ended with a table size change
  if (!m_is_response && !m_is_trailer) {
    auto req = head->as<http::RequestHead>();
    if (
      !req->method || !req->method->length() ||
      !req->scheme || !req->scheme->length() ||
      !req->path || !req->path->length()
    ) {
      // missing mandatory request headers
      return PROTOCOL_ERROR;
    }
  }
  return NO_ERROR;
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

bool HeaderDecoder::read_str(uint8_t c, bool lowercase_only) {
  if (m_prefix & 0x80) {
    const auto &tree = s_huffman_tree.get();
    int last_bit = 8;
    for (int b = 7; b >= 0; b--) {
      bool bit = (c >> b) & 1;
      m_ptr = bit ? tree[m_ptr].right : tree[m_ptr].left;
      auto &node = tree[m_ptr];
      if (!node.left) {
        auto ch = node.right;
        if (ch == 256) {
          error(); // EOS is considered an error
          return false;
        }
        if (lowercase_only) {
          if (std::tolower(ch) != ch) {
            error(PROTOCOL_ERROR);
            return false;
          }
        }
        s_dp.push(&m_buffer, char(ch));
        m_ptr = 0;
        last_bit = b;
      }
    }
    if (m_int == 1) {
      uint8_t mask = uint8_t(1 << last_bit) - 1;
      if (mask == 0xff || (c & mask) != mask) {
        error();
        return false;
      }
    }
  } else {
    if (lowercase_only) {
      if (std::tolower(c) != c) {
        error(PROTOCOL_ERROR);
        return false;
      }
    }
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
  m_entry_prefix = prefix;
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
      error();
    } else if (const auto *entry = get_entry(m_int)) {
      auto v = entry->value;
      if (!v) v = pjs::Str::empty;
      if (add_field(entry->name, v)) {
        m_state = INDEX_PREFIX;
      }
    } else {
      error();
    }
  } else if ((p & 0xe0) == 0x20) {
    if (m_int > m_settings.header_table_size) {
      error();
    } else {
      m_dynamic_table.resize(m_int);
      m_state = INDEX_PREFIX;
    }
  } else if (m_int) {
    if (const auto *entry = get_entry(m_int)) {
      m_name = entry->name;
      m_state = VALUE_PREFIX;
    } else {
      error();
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

bool HeaderDecoder::add_field(pjs::Str *name, pjs::Str *value) {
  if (name->str()[0] == ':') {
    if (m_is_trailer || m_is_pseudo_end) {
      error(PROTOCOL_ERROR);
      return false;
    } else if (m_is_response) {
      if (name == s_colon_status) {
        auto res = m_head->as<http::ResponseHead>();
        res->status = std::atoi(value->c_str());
      } else {
        error(PROTOCOL_ERROR);
        return false;
      }
    } else {
      if (name == s_colon_method) {
        auto req = m_head->as<http::RequestHead>();
        if (req->method) {
          error(PROTOCOL_ERROR);
          return false;
        } else {
          req->method = value;
        }
      } else if (name == s_colon_scheme) {
        auto req = m_head->as<http::RequestHead>();
        if (req->scheme) {
          error(PROTOCOL_ERROR);
          return false;
        } else {
          req->scheme = value;
        }
      } else if (name == s_colon_authority) {
        auto req = m_head->as<http::RequestHead>();
        pjs::Value v;
        auto headers = m_head->headers.get();
        headers->get(s_host, v);
        if (v.is_undefined()) headers->set(s_host, value);
        req->authority = value;
      } else if (name == s_colon_path) {
        auto req = m_head->as<http::RequestHead>();
        if (req->path) {
          error(PROTOCOL_ERROR);
          return false;
        } else {
          req->path = value;
        }
      } else {
        error(PROTOCOL_ERROR);
        return false;
      }
    }
  } else {
    if (
      name == s_connection ||
      name == s_keep_alive ||
      name == s_proxy_connection ||
      name == s_transfer_encoding ||
      name == s_upgrade
    ) {
      error(PROTOCOL_ERROR);
      return false;
    } else if (name == s_te && value != s_trailers) {
      error(PROTOCOL_ERROR);
      return false;
    }
    if (name == s_content_length) {
      m_content_length = std::atoi(value->c_str());
    }
    auto headers = m_head->headers.get();
    if (!headers) {
      headers = pjs::Object::make();
      m_head->headers = headers;
    }
    headers->set(name, value);
    m_is_pseudo_end = true;
  }
  return true;
}

auto HeaderDecoder::get_entry(size_t i) const -> const TableEntry* {
  auto &tab = s_static_table.get();
  if (i <= tab.size()) {
    return &tab[i - 1];
  }
  i -= tab.size() + 1;
  return m_dynamic_table.get(i);
}

void HeaderDecoder::new_entry(pjs::Str *name, pjs::Str *value) {
  m_dynamic_table.add(name, value);
}

void HeaderDecoder::error(ErrorCode err) {
  m_state = ERROR;
  m_error = err;
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

thread_local HeaderEncoder::StaticTable HeaderEncoder::m_static_table;

void HeaderEncoder::encode(bool is_response, bool is_tail, pjs::Object *head, Data &data) {
  Data::Builder db(data, &s_dp);
  bool has_authority = false;
  if (!is_tail) {
    if (is_response) {
      pjs::Ref<http::ResponseHead> h = pjs::coerce<http::ResponseHead>(head);
      auto status = h->status;
      if (status == 200) {
        encode_header_field(db, s_colon_status, s_200);
      } else {
        pjs::Ref<pjs::Str> str(pjs::Str::make(status));
        encode_header_field(db, s_colon_status, str);
      }

    } else {
      pjs::Ref<http::RequestHead> h = pjs::coerce<http::RequestHead>(head);
      auto method = h->method.get();
      auto scheme = h->scheme.get();
      auto path = h->path.get();
      auto authority = h->authority.get();

      if (!method || !method->length()) method = s_GET;
      if (!scheme || !scheme->length()) scheme = s_http;
      if (!path || !path->length()) path = s_root_path;

      encode_header_field(db, s_colon_method, method);
      encode_header_field(db, s_colon_scheme, scheme);
      encode_header_field(db, s_colon_path, path);

      if (authority && authority->length() > 0) {
        encode_header_field(db, s_colon_authority, authority);
        has_authority = true;
      }
    }
  }

  pjs::Value headers;
  if (head) head->get(s_headers, headers);
  if (headers.is_object()) {
    if (auto obj = headers.o()) {
      obj->iterate_all(
        [&](pjs::Str *k, pjs::Value &v) {
          if (k == pjs::Str::empty) return;
          if (k == s_host) {
            if (has_authority) return;
            k = s_colon_authority;
          }
          if (k == s_connection) return;
          if (k == s_keep_alive) return;
          if (k == s_proxy_connection) return;
          if (k == s_transfer_encoding) return;
          if (k == s_upgrade) return;
          auto s = v.to_string();
          encode_header_field(db, k, s);
          s->release();
        }
      );
    }
  }

  db.flush();
}

void HeaderEncoder::encode_header_field(Data::Builder &db, pjs::Str *k, pjs::Str *v) {
  if (const auto *ent = m_static_table.find(k)) {
    auto i = ent->values.find(v);
    if (i == ent->values.end()) {
      encode_int(db, 0x00, 4, ent->index);
      encode_str(db, v, false);
    } else {
      encode_int(db, 0x80, 1, i->second);
    }
  } else {
    encode_int(db, 0x00, 4, 0);
    encode_str(db, k, true);
    encode_str(db, v, false);
  }
}

void HeaderEncoder::encode_int(Data::Builder &db, uint8_t prefix, int prefix_len, uint32_t n) {
  uint8_t mask = (1 << (8 - prefix_len)) - 1;
  if (n < mask) {
    db.push(uint8_t(prefix | n));
  } else {
    db.push(uint8_t(prefix | mask));
    n -= mask;
    while (n) {
      if (n >> 7) {
        db.push(uint8_t(0x80 | (n & 0x7f)));
      } else {
        db.push(uint8_t(n & 0x7f));
      }
      n >>= 7;
    }
  }
}

void HeaderEncoder::encode_str(Data::Builder &db, pjs::Str *s, bool lowercase) {
  encode_int(db, 0, 1, s->size());
  if (lowercase) {
    for (auto ch : s->str()) {
      db.push(char(std::tolower(ch)));
    }
  } else {
    db.push(s->str());
  }
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
// Endpoint
//

std::atomic<uint32_t> Endpoint::s_endpoint_id(0);

thread_local bool Endpoint::s_metrics_initialized = false;
thread_local int Endpoint::s_server_stream_count = 0;
thread_local int Endpoint::s_client_stream_count = 0;

Endpoint::Options::Options(pjs::Object *options) {
  Value(options, "connectionWindowSize")
    .get_binary_size(connection_window_size)
    .check_nullable();
  Value(options, "streamWindowSize")
    .get_binary_size(stream_window_size)
    .check_nullable();
}

Endpoint::Endpoint(bool is_server_side, const Options &options)
  : m_id(s_endpoint_id.fetch_add(1, std::memory_order_relaxed))
  , m_options(options)
  , m_header_decoder(m_settings)
  , m_is_server_side(is_server_side)
{
  init_metrics();
  m_settings.enable_push = false;
  m_settings.initial_window_size = options.stream_window_size;
  m_recv_window_max = options.connection_window_size;
  m_recv_window_low = m_recv_window_max / 2;
}

Endpoint::~Endpoint() {
  for_each_stream(
    [this](StreamBase *s) {
      m_stream_map.set(s->m_id, nullptr);
      delete s;
      return true;
    }
  );
}

void Endpoint::init_settings(const uint8_t *data, size_t size) {
  m_peer_settings.decode(data, size);
}

void Endpoint::process_event(Event *evt) {
  if (m_has_gone_away) return;

  if (auto data = evt->as<Data>()) {
    if (!data->empty()) {
#if DEBUG_HTTP2
      debug_dump_i(*data);
#endif
      FrameDecoder::deframe(data);
    }

  } else if (evt->is<StreamEnd>()) {
    end_all();
    on_output(StreamEnd::make());
  }
}

auto Endpoint::stream_open(int id) -> StreamBase* {
  auto stream = on_new_stream(id);
  stream->m_send_window = m_peer_settings.initial_window_size;
  m_streams.push(stream);
  m_stream_map.set(id, stream);
  return stream;
}

void Endpoint::stream_close(int id) {
  if (auto s = m_stream_map.set(id, nullptr)) {
    s->set_pending(false);
    m_streams.remove(s);
    on_delete_stream(s);
    if (m_has_shutdown) shutdown();
  }
}

void Endpoint::stream_error(int id, ErrorCode err) {
  stream_close(id);
  FrameEncoder::RST_STREAM(id, err, m_output_buffer);
  FlushTarget::need_flush();
}

void Endpoint::connection_error(ErrorCode err) {
  end_all();
  FrameEncoder::GOAWAY(m_last_received_stream_id, err, m_output_buffer);
  on_output(Data::make(std::move(m_output_buffer)));
  on_output(StreamEnd::make());
}

void Endpoint::shutdown() {
  if (m_streams.empty() && m_streams_pending.empty()) {
    connection_error(ErrorCode::NO_ERROR);
  } else {
    m_has_shutdown = true;
  }
}

void Endpoint::on_flush() {
  send_window_updates();
  flush();
}

void Endpoint::on_deframe(Frame &frm) {
#if DEBUG_HTTP2
  debug_dump_i(frm);
#endif
  if (m_header_decoder.started() && frm.type != Frame::CONTINUATION) {
    connection_error(PROTOCOL_ERROR);
  } else if (auto id = frm.stream_id) {
    if (
      frm.type == Frame::SETTINGS ||
      frm.type == Frame::PING ||
      frm.type == Frame::GOAWAY
    ) {
      connection_error(PROTOCOL_ERROR);
    } else {
      auto stream = m_stream_map.get(id);
      if (!stream) {
        if (id <= m_last_received_stream_id) {
          if (
            frm.type == Frame::PRIORITY ||
            frm.type == Frame::RST_STREAM ||
            frm.type == Frame::WINDOW_UPDATE
          ) return; // ignore PRIORITY for closed streams
          connection_error(STREAM_CLOSED);
          return;
        }
        if (frm.type == Frame::DATA || frm.type == Frame::WINDOW_UPDATE) {
          connection_error(PROTOCOL_ERROR);
          return;
        }
        if (!m_is_server_side) {
          // don't accept new streams as a client
          return;
        }
        if ((id & 1) == 0) {
          connection_error(PROTOCOL_ERROR);
          return;
        }
        stream = on_new_stream(id);
        stream->m_send_window = m_peer_settings.initial_window_size;
        m_streams.push(stream);
        m_stream_map.set(id, stream);
        if (frm.type != Frame::PRIORITY) {
          m_last_received_stream_id = id;
        }
      }
      stream->on_frame(frm);
    }
  } else {
    switch (frm.type) {
      case Frame::SETTINGS:
        if (frm.is_ACK()) {
          if (!frm.payload.empty()) {
            connection_error(FRAME_SIZE_ERROR);
          }
        } else {
          auto len = frm.payload.size();
          if (len % 6) {
            connection_error(FRAME_SIZE_ERROR);
          } else if (len <= Settings::MAX_SIZE) {
            uint8_t buf[len];
            frm.payload.to_bytes(buf);
            auto old_initial_window_size = m_peer_settings.initial_window_size;
            auto err = m_peer_settings.decode(buf, len);
            if (err == NO_ERROR) {
              bool ok = true;
              if (m_peer_settings.initial_window_size != old_initial_window_size) {
                auto delta = m_peer_settings.initial_window_size - old_initial_window_size;
                ok = for_each_stream(
                  [=](StreamBase *s) {
                    return s->update_send_window(delta);
                  }
                );
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
      case Frame::PING:
        if (frm.payload.size() != 8) {
          connection_error(FRAME_SIZE_ERROR);
        } else if (!frm.is_ACK()) {
          frm.flags |= Frame::BIT_ACK;
          frame(frm);
        }
        break;
      case Frame::GOAWAY:
        connection_error(NO_ERROR);
        break;
      case Frame::WINDOW_UPDATE: {
        auto inc = 0;
        auto err = frm.decode_window_update(inc);
        if (err == NO_ERROR) {
          if (!inc) {
            connection_error(PROTOCOL_ERROR);
          } else {
            auto n = (uint32_t)m_send_window + (uint32_t)inc;
            if (n > 0x7fffffff) {
              connection_error(FLOW_CONTROL_ERROR);
            } else {
              m_send_window = n;
              for_each_pending_stream(
                [this](StreamBase *s) {
                  s->update_connection_send_window();
                  return m_send_window > 0;
                }
              );
            }
          }
        } else {
          connection_error(err);
        }
        break;
      }
      case Frame::DATA:
      case Frame::HEADERS:
      case Frame::PRIORITY:
      case Frame::RST_STREAM:
      case Frame::CONTINUATION:
        connection_error(PROTOCOL_ERROR);
        break;
      default: break;
    }
  }
}

void Endpoint::on_deframe_error(ErrorCode err) {
  connection_error(err);
}

bool Endpoint::for_each_stream(const std::function<bool(StreamBase*)> &cb) {
  if (!for_each_pending_stream(cb)) return false;
  for (auto *p = m_streams.head(); p; ) {
    auto *s = p; p = p->next();
    if (!cb(s)) return false;
  }
  return true;
}

bool Endpoint::for_each_pending_stream(const std::function<bool(StreamBase*)> &cb) {
  for (auto *p = m_streams_pending.head(); p; ) {
    auto *s = p; p = p->next();
    if (!cb(s)) return false;
  }
  return true;
}

void Endpoint::send_window_updates() {
  if (m_has_gone_away) return;

  if (m_recv_window < m_recv_window_max) {
    Frame frm;
    frm.stream_id = 0;
    frm.type = Frame::WINDOW_UPDATE;
    frm.flags = 0;
    frm.encode_window_update(m_recv_window_max - m_recv_window);
#if DEBUG_HTTP2
    debug_dump_o(frm);
#endif
    FrameEncoder::frame(frm, m_output_buffer);
    m_recv_window = m_recv_window_max;
  }

  for (auto *s = m_streams_pending.head(); s;) {
    if (!s->m_is_clearing) break;
    if (s->m_recv_window < s->m_recv_window_max) {
      Frame frm;
      frm.stream_id = s->m_id;
      frm.type = Frame::WINDOW_UPDATE;
      frm.flags = 0;
      frm.encode_window_update(s->m_recv_window_max - s->m_recv_window);
#if DEBUG_HTTP2
      debug_dump_o(frm);
#endif
      FrameEncoder::frame(frm, m_output_buffer);
      s->m_recv_window = s->m_recv_window_max;
    }
    auto stream = s; s = s->next();
    stream->set_clearing(false);
  }
}

void Endpoint::frame(Frame &frm) {
  if (m_has_gone_away) return;

  // Send preface if not yet
  if (!m_has_sent_preface) {
    m_has_sent_preface = true;
    if (!m_is_server_side) {
      thread_local static Data s_preface("PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n", &s_dp);
      m_output_buffer.push(s_preface);
    }
    uint8_t buf[Settings::MAX_SIZE];
    auto len = m_settings.encode(buf);
    Frame frm;
    frm.stream_id = 0;
    frm.type = Frame::SETTINGS;
    frm.flags = 0;
    frm.payload.push(buf, len, &s_dp);
#if DEBUG_HTTP2
    debug_dump_o(frm);
#endif
    FrameEncoder::frame(frm, m_output_buffer);
  }

  // Send window updates
  send_window_updates();

#if DEBUG_HTTP2
  debug_dump_o(frm);
#endif

  // Send the frame
  FrameEncoder::frame(frm, m_output_buffer);
  FlushTarget::need_flush();
}

void Endpoint::flush() {
  if (!m_output_buffer.empty()) {
    Data data;
    data.pack(m_output_buffer, &s_dp, 1);
#if DEBUG_HTTP2
    debug_dump_o(data);
#endif
    on_output(Data::make(std::move(data)));
    m_output_buffer.clear();
  }
}

void Endpoint::end_all() {
  m_has_gone_away = true;
  for_each_pending_stream(
    [](StreamBase *s) {
      s->set_pending(false);
      return true;
    }
  );
  for_each_stream(
    [](StreamBase *s) {
      s->end();
      return true;
    }
  );
}

void Endpoint::debug_dump_i() const {
#if DEBUG_HTTP2
  std::cerr << Log::format_elapsed_time();
  std::cerr << " http2 ";
  std::cerr << " endpoint #" << std::left << std::setw(3) << m_id;
  std::cerr << (m_is_server_side ? "| >> |    |" : "|    | << |");
#endif
}

void Endpoint::debug_dump_o() const {
#if DEBUG_HTTP2
  std::cerr << Log::format_elapsed_time();
  std::cerr << " http2 ";
  std::cerr << " endpoint #" << std::left << std::setw(3) << m_id;
  std::cerr << (m_is_server_side ? "| << |    |" : "|    | >> |");
#endif
}

void Endpoint::debug_dump_i(const Data &data) const {
#if DEBUG_HTTP2
  if (Log::is_enabled(Log::HTTP2)) {
    debug_dump_i();
    std::cerr << " Recv " << data.size() << std::endl;
  }
#endif
}

void Endpoint::debug_dump_o(const Data &data) const {
#if DEBUG_HTTP2
  if (Log::is_enabled(Log::HTTP2)) {
    debug_dump_o();
    std::cerr << " Send " << data.size() << std::endl;
  }
#endif
}

void Endpoint::debug_dump_i(const Frame &frm) const {
#if DEBUG_HTTP2
  if (Log::is_enabled(Log::HTTP2)) {
    debug_dump_i();
    std::cerr << "   ";
    frm.debug_dump(std::cerr);
    std::cerr << std::endl;
  }
#endif
}

void Endpoint::debug_dump_o(const Frame &frm) const {
#if DEBUG_HTTP2
  if (Log::is_enabled(Log::HTTP2)) {
    debug_dump_o();
    std::cerr << "   ";
    frm.debug_dump(std::cerr);
    std::cerr << std::endl;
  }
#endif
}

void Endpoint::init_metrics() {
  if (!s_metrics_initialized) {
    thread_local static pjs::ConstStr s_server("Server");
    thread_local static pjs::ConstStr s_client("Client");

    pjs::Ref<pjs::Array> label_names = pjs::Array::make();
    label_names->length(1);
    label_names->set(0, "type");

    stats::Gauge::make(
      pjs::Str::make("pipy_http2_stream_count"),
      label_names,
      [=](stats::Gauge *gauge) {
        pjs::Str *server = s_server;
        pjs::Str *client = s_client;
        gauge->with_labels(&server, 1)->set(s_server_stream_count);
        gauge->with_labels(&client, 1)->set(s_client_stream_count);
        gauge->set(s_server_stream_count + s_client_stream_count);
      }
    );

    s_metrics_initialized = true;
  }
}

//
// Endpoint::StreamBase
//
// For server-side: on_frame() -> event() ---(I)--> pipeline ---(O)--> on_event() -> frame()
// For client-side: on_event() -> frame() ---(O)--> pipeline ---(I)--> on_frame() -> event()
//
// A StreamBase is recycled when both its input and output ended.
//   - Input is ended passively as receiving a RST_STREAM
//   - Output is ended actively when a server shuts down or a client aborts a stream
//
// Which end is the input or output depends on what side the endpoint is:
//   - For server-side, the input/output are the same ends as the pipeline for the stream
//   - For client-size, the input/output are opposite to the ends of the pipeline for the stream
//

static const int MAX_HEADER_FRAME_SIZE = 1024;

Endpoint::StreamBase::StreamBase(
  Endpoint *endpoint,
  int id, bool is_server_side
) : m_endpoint(endpoint)
  , m_id(id)
  , m_is_server_side(is_server_side)
  , m_header_decoder(endpoint->m_header_decoder)
  , m_header_encoder(endpoint->m_header_encoder)
  , m_recv_window(endpoint->m_settings.initial_window_size)
  , m_recv_window_max(m_recv_window)
  , m_recv_window_low(m_recv_window_max / 2)
  , m_peer_settings(endpoint->m_peer_settings)
{
  if (is_server_side) {
    s_server_stream_count++;
  } else {
    s_client_stream_count++;
  }
}

Endpoint::StreamBase::~StreamBase() {
  if (m_is_server_side) {
    s_server_stream_count--;
  } else {
    s_client_stream_count--;
  }
}

void Endpoint::StreamBase::input(Event *evt) {
  if (auto start = evt->as<MessageStart>()) {
    if (!m_is_message_started) {
      Data buf;
      m_header_encoder.encode(m_is_server_side, false, start->head(), buf);
      write_header_block(buf);
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
      m_is_message_started = true;
    }

  } else if (auto data = evt->as<Data>()) {
    if (m_is_message_started && !data->empty()) {
      if (m_state == OPEN || m_state == HALF_CLOSED_REMOTE) {
        m_send_buffer.push(*data);
        pump();
        set_pending(true);
        flush();
      }
    }

  } else if (
    (evt->is<MessageEnd>() && !m_is_tunnel) ||
    (evt->is<StreamEnd>()))
  {
    if (m_is_message_started && !m_is_message_ended) {
      if (auto *end = evt->as<MessageEnd>()) {
        if (auto tail = end->tail()) {
          m_header_encoder.encode(m_is_server_side, true, tail, m_tail_buffer);
        }
      }
      if (m_state == OPEN) {
        m_state = HALF_CLOSED_LOCAL;
      } else if (m_state == HALF_CLOSED_REMOTE) {
        m_state = CLOSED;
      }
      m_is_message_ended = true;
      m_end_stream_send = true;
      pump();
    }
  }
}

void Endpoint::StreamBase::end_input() {
  if (!m_end_input) {
    m_end_input = true;
    recycle();
  }
}

void Endpoint::StreamBase::end_output() {
  if (!m_end_output) {
    m_end_output = true;
    recycle();
  }
}

void Endpoint::StreamBase::on_frame(Frame &frm) {
  switch (frm.type) {
    case Frame::DATA: {
      if (m_state == OPEN || m_state == HALF_CLOSED_LOCAL) {
        if (frm.is_PADDED() && !parse_padding(frm)) break;
        auto size = frm.payload.size();
        if (size > 0) {
          if (!deduct_recv(size)) break;
          output(Data::make(frm.payload));
          m_recv_payload_size += size;
        }
        if (frm.is_END_STREAM()) {
          set_clearing(false);
          if (m_state == OPEN) {
            m_state = HALF_CLOSED_REMOTE;
            check_content_length();
            stream_end(nullptr);
          } else if (m_state == HALF_CLOSED_LOCAL) {
            m_state = CLOSED;
            check_content_length();
            stream_end(nullptr);
          }
        }
      } else {
        stream_error(STREAM_CLOSED);
      }
      break;
    }

    case Frame::HEADERS: {
      if (m_end_headers && !frm.is_END_STREAM()) {
        stream_error(PROTOCOL_ERROR);
      } else if (
        m_state == IDLE ||
        m_state == RESERVED_REMOTE ||
        m_state == OPEN ||
        m_state == HALF_CLOSED_LOCAL
      ) {
        if (frm.is_PADDED() && !parse_padding(frm)) break;
        if (frm.is_PRIORITY() && !parse_priority(frm)) break;
        if (frm.is_END_STREAM()) m_end_stream_recv = true;
        m_header_decoder.start(!m_is_server_side, m_end_headers);
        parse_headers(frm);
      } else {
        stream_error(STREAM_CLOSED);
      }
      break;
    }

    case Frame::PRIORITY: {
      parse_priority(frm);
      break;
    }

    case Frame::RST_STREAM: {
      if (m_state == IDLE) {
        connection_error(PROTOCOL_ERROR);
      } else if (frm.payload.size() != 4) {
        connection_error(FRAME_SIZE_ERROR);
      } else {
        m_state = CLOSED;
        stream_end(nullptr);
      }
      break;
    }

    case Frame::PUSH_PROMISE: {
      if (m_is_server_side) {
        connection_error(PROTOCOL_ERROR);
      } else {
        // TODO
      }
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

    default: break;
  }
}

bool Endpoint::StreamBase::parse_padding(Frame &frm) {
  uint8_t pad_length = 0;
  frm.payload.shift(1, &pad_length);
  if (pad_length >= frm.payload.size()) {
    connection_error(PROTOCOL_ERROR);
    return false;
  }
  frm.payload.pop(pad_length);
  return true;
}

bool Endpoint::StreamBase::parse_priority(Frame &frm) {
  if (frm.payload.size() < 5) {
    connection_error(PROTOCOL_ERROR);
    return false;
  }
  uint8_t buf[5];
  frm.payload.shift(sizeof(buf), buf);
  auto dependency = 0x7fffffff & (
    ((uint32_t)buf[0] << 24)|
    ((uint32_t)buf[1] << 16)|
    ((uint32_t)buf[2] <<  8)|
    ((uint32_t)buf[3] <<  0)
  );
  if (dependency == m_id) {
    connection_error(PROTOCOL_ERROR);
    return false;
  }
  return true;
}

void Endpoint::StreamBase::parse_headers(Frame &frm) {
  auto err = m_header_decoder.decode(frm.payload);
  if (err != NO_ERROR) {
    connection_error(err);
    return;
  }

  if (frm.is_END_HEADERS()) {
    pjs::Ref<http::MessageHead> head;
    auto err = m_header_decoder.end(head);
    if (err != NO_ERROR) {
      connection_error(COMPRESSION_ERROR);
      return;
    }

    if (m_state == IDLE) {
      m_state = OPEN;
    } else if (m_state == RESERVED_REMOTE) {
      m_state = HALF_CLOSED_LOCAL;
    }

    http::MessageTail *tail = nullptr;

    if (m_header_decoder.is_trailer()) {
      tail = http::MessageTail::make();
      tail->headers = head->headers;

    } else {
      m_end_headers = true;
      output(MessageStart::make(head));
    }

    if (m_is_server_side) {
      if (head->as<http::RequestHead>()->method == s_CONNECT) {
        m_is_tunnel = true;
        output(MessageEnd::make());
      }
    } else if (m_is_tunnel) {
      output(MessageEnd::make());
    }

    if (m_end_stream_recv) {
      if (m_state == OPEN) {
        m_state = HALF_CLOSED_REMOTE;
        check_content_length();
        stream_end(tail);
      } else if (m_state == HALF_CLOSED_LOCAL) {
        m_state = CLOSED;
        check_content_length();
        stream_end(tail);
      }
    }
  }
}

void Endpoint::StreamBase::check_content_length() {
  auto content_length = m_header_decoder.content_length();
  if (content_length >= 0 && content_length != m_recv_payload_size) {
    connection_error(PROTOCOL_ERROR);
  }
}

bool Endpoint::StreamBase::deduct_recv(int size) {
  if (size > m_recv_window) {
    connection_error(FLOW_CONTROL_ERROR);
    return false;
  }
  auto &connection_recv_window = m_endpoint->m_recv_window;
  if (size > connection_recv_window) {
    connection_error(FLOW_CONTROL_ERROR);
    return false;
  }
  connection_recv_window -= size;
  m_recv_window -= size;
  if (m_recv_window <= m_recv_window_low) set_clearing(true);
  if (m_is_clearing || connection_recv_window <= m_endpoint->m_recv_window_low) flush();
  return true;
}

auto Endpoint::StreamBase::deduct_send(int size) -> int {
  auto &win_size = m_endpoint->m_send_window;
  if (size > win_size) {
    size = win_size;
  }
  win_size -= size;
  return size;
}

bool Endpoint::StreamBase::update_send_window(int delta) {
  if (!delta) {
    stream_error(PROTOCOL_ERROR);
    return false;
  }
  if (delta > 0 && m_send_window > 0) {
    auto n = (uint32_t)m_send_window + (uint32_t)delta;
    if (n > 0x7fffffff) {
      stream_error(FLOW_CONTROL_ERROR);
      return false;
    }
  }
  m_send_window += delta;
  pump();
  recycle();
  return true;
}

void Endpoint::StreamBase::update_connection_send_window() {
  pump();
  recycle();
}

void Endpoint::StreamBase::write_header_block(Data &data) {
  Frame frm;
  frm.stream_id = m_id;
  frm.type = Frame::HEADERS;
  frm.flags = m_end_stream_send ? Frame::BIT_END_STREAM : 0;
  while (!data.empty()) {
    auto len = std::min(data.size(), MAX_HEADER_FRAME_SIZE);
    if (len == data.size()) frm.flags |= Frame::BIT_END_HEADERS;
    data.shift(len, frm.payload);
    frame(frm);
    frm.type = Frame::CONTINUATION;
    frm.payload.clear();
  }
}

void Endpoint::StreamBase::stream_end(http::MessageTail *tail) {
  if (m_is_tunnel) {
    output(StreamEnd::make());
  } else {
    output(MessageEnd::make(tail));
    output(StreamEnd::make());
  }
  end_input();
}

void Endpoint::StreamBase::set_pending(bool pending) {
  if (pending != m_is_pending) {
    if (pending) {
      if (m_endpoint->m_has_gone_away) return;
      m_endpoint->m_streams.remove(this);
      m_endpoint->m_streams_pending.push(this);
    } else {
      if (m_is_clearing) return;
      m_endpoint->m_streams_pending.remove(this);
      m_endpoint->m_streams.push(this);
      m_is_clearing = false;
    }
    m_is_pending = pending;
  }
}

void Endpoint::StreamBase::set_clearing(bool clearing) {
  if (clearing != m_is_clearing) {
    if (clearing) {
      if (m_endpoint->m_has_gone_away) return;
      if (m_is_pending) {
        m_endpoint->m_streams_pending.remove(this);
        m_endpoint->m_streams_pending.unshift(this);
      } else {
        m_endpoint->m_streams.remove(this);
        m_endpoint->m_streams_pending.unshift(this);
        m_is_pending = true;
      }
    } else {
      if (m_is_pending) {
        m_endpoint->m_streams_pending.remove(this);
        if (m_send_buffer.empty()) {
          m_endpoint->m_streams.push(this);
          m_is_pending = false;
        } else {
          m_endpoint->m_streams_pending.push(this);
        }
      }
    }
    m_is_clearing = clearing;
  }
}

void Endpoint::StreamBase::pump() {
  bool is_empty_end = (m_end_stream_send && m_send_buffer.empty() && m_tail_buffer.empty());
  int size = m_send_buffer.size();
  if (size > m_send_window) size = m_send_window;
  if (size > 0) size = deduct_send(size);
  if (size > 0 || is_empty_end) {
    auto remain = size;
    do {
      auto n = std::min(remain, m_peer_settings.max_frame_size);
      remain -= n;
      Frame frm;
      frm.stream_id = m_id;
      frm.type = Frame::DATA;
      if (n > 0) m_send_buffer.shift(n, frm.payload);
      if (m_end_stream_send && m_send_buffer.empty() && m_tail_buffer.empty()) {
        frm.flags = Frame::BIT_END_STREAM;
        m_end_stream_send = false;
      } else {
        frm.flags = 0;
      }
      frame(frm);
    } while (remain > 0);
    m_send_window -= size;
  }
  if (m_send_buffer.empty()) {
    if (!m_tail_buffer.empty()) write_header_block(m_tail_buffer);
    set_pending(false);
  } else {
    set_pending(true);
  }
}

void Endpoint::StreamBase::recycle() {
  if (m_end_input && m_end_output && m_send_buffer.empty()) {
    close();
  }
}

//
// Server
//

Server::Server(const Options &options) : Endpoint(true, options)
{
}

Server::~Server() {
  delete m_initial_stream;
}

auto Server::initial_stream() -> Input* {
  if (!m_initial_stream) {
    m_initial_stream = new InitialStream();
  }
  return m_initial_stream->input();
}

void Server::init() {
  if (m_initial_stream) {
    if (auto msg = m_initial_stream->initial_request()) {
      auto *s = stream_open(1);
      pjs::Ref<http::RequestHead> head = pjs::coerce<http::RequestHead>(msg->head());
      if (auto headers = head->headers.get()) {
        pjs::Value settings;
        headers->get(s_http2_settings, settings);
        if (settings.is_string()) {
          const auto &b64 = settings.s()->str();
          const auto size = b64.size() / 4 * 3 + 3;
          if (size < Settings::MAX_SIZE) {
            uint8_t buf[size];
            auto len = utils::decode_base64url(buf, b64.c_str(), b64.length());
            init_settings(buf, len);
          }
        }
      }
      msg->write(static_cast<Stream*>(s)->EventSource::output());
    }
    delete m_initial_stream;
    m_initial_stream = nullptr;
  }
}

//
// Server::Stream
//

Server::Stream::Stream(Server *server, int id)
  : StreamBase(server, id, true)
{
  auto p = server->on_new_stream_pipeline(EventSource::reply());
  EventSource::chain(p->input());
  m_pipeline = p;
}

Server::Stream::~Stream() {
  PipelineBase::auto_release(m_pipeline);
}

//
// Client
//

Client::Client(const Options &options) : Endpoint(false, options)
{
}

void Client::open(EventFunction *session) {
  EventSource::chain(session->input());
  session->chain(EventSource::reply());
}

auto Client::stream() -> EventFunction* {
  auto id = (m_last_sent_stream_id += 2);
  auto stream = Endpoint::stream_open(id);
  return static_cast<Stream*>(stream);
}

void Client::close(EventFunction *stream) {
  auto *s = static_cast<Stream*>(stream);
  s->end_output();
}

//
// Client::Stream
//

Client::Stream::Stream(Client *client, int id)
  : StreamBase(client, id, false)
{
}

} // namespace http2
} // namespace pipy
