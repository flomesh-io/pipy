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

#ifndef ALLOCATOR_HPP
#define ALLOCATOR_HPP

namespace pipy {

//
// PooledAllocator
//

template<typename T>
class PooledAllocator {
public:
  using value_type = T;

  template<typename U>
  struct rebind {
    using other = PooledAllocator<U>;
  };

  PooledAllocator() = default;

  template<typename U>
  PooledAllocator(const PooledAllocator<U> &other) {};

  T* allocate(size_t n) {
    if (auto p = m_pool) {
      m_pool = *reinterpret_cast<T**>(p);
      return p;
    } else {
      return static_cast<T*>(std::malloc(std::max(sizeof(T), sizeof(T*))));
    }
  }

  void deallocate(T *p, size_t n) {
    *reinterpret_cast<T**>(p) = m_pool;
    m_pool = p;
  }

private:
  thread_local static T* m_pool;
};

template<typename T>
thread_local T* PooledAllocator<T>::m_pool = nullptr;

} // namespace pipy

#endif // ALLOCATOR_HPP
