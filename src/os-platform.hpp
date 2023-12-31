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

#ifndef OS_PLATFORM_HPP
#define OS_PLATFORM_HPP

#ifdef _WIN32

#include <Windows.h>

// Remove name pollution from Windows.h
#undef NO_ERROR
#undef DELETE
#undef ERROR
#undef s_addr
#undef s_host
#undef s_net
#undef s_imp
#undef s_impno
#undef s_lh

#define SIGNAL_STOP   SIGINT
#define SIGNAL_RELOAD SIGBREAK
#define SIGNAL_ADMIN  SIGTERM

#include <string>

namespace pipy {

auto Win32_GetLastError(const std::string &function) -> std::string;

} // namespace pipy

#else // !_WIN32

#include <signal.h>

#define SIGNAL_STOP   SIGINT
#define SIGNAL_RELOAD SIGHUP
#define SIGNAL_ADMIN  SIGTSTP

#endif // _WIN32

#endif // OS_PLATFORM_HPP
