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

#ifndef THROTTLE_HPP
#define THROTTLE_HPP

#include "filter.hpp"
#include "input.hpp"
#include "list.hpp"
#include "api/algo.hpp"

#include <memory>
#include <unordered_map>

namespace pipy {

//
// ThrottleBase
//

class ThrottleBase :
  public Filter,
  public algo::Quota::Consumer
{
public:
  ThrottleBase(pjs::Object *quota);

protected:
  ThrottleBase(const ThrottleBase &r);
  ~ThrottleBase();

  virtual void reset() override;
  virtual void process(Event *evt) override;
  virtual auto consume(Event *evt, algo::Quota *quota) -> Event* = 0;

  pjs::Ref<algo::Quota> m_quota;
  pjs::Ref<pjs::Function> m_quota_f;
  EventBuffer m_buffer;
  pjs::Ref<InputSource::Tap> m_closed_tap;

  void pause();
  void resume();

  virtual void on_consume(algo::Quota *quota) override;
};

//
// ThrottleMessageRate
//

class ThrottleMessageRate : public ThrottleBase {
public:
  using ThrottleBase::ThrottleBase;

protected:
  virtual auto clone() -> Filter* override;
  virtual auto consume(Event *evt, algo::Quota *quota) -> Event* override;
  virtual void dump(Dump &d) override;
};

//
// ThrottleDataRate
//

class ThrottleDataRate : public ThrottleBase {
public:
  using ThrottleBase::ThrottleBase;

protected:
  virtual auto clone() -> Filter* override;
  virtual auto consume(Event *evt, algo::Quota *quota) -> Event* override;
  virtual void dump(Dump &d) override;
};

//
// ThrottleConcurrency
//

class ThrottleConcurrency : public ThrottleBase {
public:
  using ThrottleBase::ThrottleBase;

protected:
  virtual auto clone() -> Filter* override;
  virtual void reset() override;
  virtual auto consume(Event *evt, algo::Quota *quota) -> Event* override;
  virtual void dump(Dump &d) override;

private:
  bool m_active = false;
};

} // namespace pipy

#endif // THROTTLE_HPP
