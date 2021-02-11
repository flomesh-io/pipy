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

#ifndef POOL_HPP
#define POOL_HPP

#include "ns.hpp"

#include <cstdlib>
#include <cstdint>
#include <list>
#include <vector>

NS_BEGIN

//
// Pool
//

template<typename T>
class Pool {
public:
  T* alloc() {
    if (auto p = m_free) {
      m_free = *(void**)p;
      return (T*)p;
    } else {
      return (T*)new char[sizeof(T)];
    }
  }

  void free(void *data) {
    *(void**)data = m_free;
    m_free = data;
  }

protected:
  void* m_free = nullptr;
};

//
// Pooled
//

template<typename T>
class Pooled {
public:
  void* operator new(size_t) {
    return s_pool.alloc();
  }

  void operator delete(void *p) {
    s_pool.free(p);
  }

private:
  static Pool<T> s_pool;
};

template<typename T>
Pool<T> Pooled<T>::s_pool;

NS_END

#endif // POOL_HPP
