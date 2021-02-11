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

#ifndef OBJECT_HPP
#define OBJECT_HPP

#include "ns.hpp"
#include "pool.hpp"

#include <functional>
#include <memory>
#include <string>

NS_BEGIN

//
// Object base
//

struct Object {
  typedef std::function<void(std::unique_ptr<Object>)> Receiver;

  enum Type {
    InvalidType,
    Data,
    SessionStart,
    SessionEnd,
    MessageStart,
    MessageEnd,
    MapStart,
    MapKey,
    MapEnd,
    ListStart,
    ListEnd,
    NullValue,
    BoolValue,
    IntValue,
    LongValue,
    DoubleValue,
    StringValue,
    MaxType,
  };

  virtual ~Object() {}
  virtual auto type() const -> Type = 0;
  virtual auto name() const -> const char* = 0;
  virtual auto clone() const -> Object* = 0;

  template<class T> auto is() const -> bool {
    return dynamic_cast<const T*>(this) != nullptr;
  }

  template<class T> auto as() const -> const T* {
    return dynamic_cast<const T*>(this);
  }

  template<class T> auto as() -> T* {
    return dynamic_cast<T*>(this);
  }
};

template<typename T, typename... Args>
std::unique_ptr<T> make_object(Args&&... args) {
  return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

static inline std::unique_ptr<Object> clone_object(const std::unique_ptr<Object> &other) {
  return std::unique_ptr<Object>(other->clone());
}

//
// Session objects
//

struct SessionStart : public Object, public Pooled<SessionStart> {
  virtual auto type() const -> Type override {
    return Object::SessionStart;
  }
  virtual auto name() const -> const char* override {
    return "SessionStart";
  }
  virtual auto clone() const -> Object* override {
    return new SessionStart();
  }
};

struct SessionEnd : public Object, public Pooled<SessionEnd> {
  enum Error {
    NO_ERROR = 0,
    UNKNOWN_ERROR,
    CANNOT_RESOLVE,
    CONNECTION_REFUSED,
    UNAUTHORIZED,
    READ_ERROR,
  };
  Error error = NO_ERROR;
  std::string message;
  SessionEnd(Error error = NO_ERROR) { this->error = error; }
  SessionEnd(const std::string &message, Error error = UNKNOWN_ERROR) {
    this->message = message;
    this->error = error;
  }
  virtual auto type() const -> Type override {
    return Object::SessionEnd;
  }
  virtual auto name() const -> const char* override {
    return "SessionEnd";
  }
  virtual auto clone() const -> Object* override {
    return new SessionEnd();
  }
};

//
// Message objects
//

struct MessageStart : public Object, public Pooled<MessageStart> {
  virtual auto type() const -> Type override {
    return Object::MessageStart;
  }
  virtual auto name() const -> const char* override {
    return "MessageStart";
  }
  virtual auto clone() const -> Object* override {
    return new MessageStart();
  }
};

struct MessageEnd : public Object, public Pooled<MessageEnd> {
  virtual auto type() const -> Type override {
    return Object::MessageEnd;
  }
  virtual auto name() const -> const char* override {
    return "MessageEnd";
  }
  virtual auto clone() const -> Object* override {
    return new MessageEnd();
  }
};

//
// Value object base
//

struct ValueObject : public Object {
};

struct PrimitiveObject : public ValueObject {
  virtual auto to_string() const -> std::string = 0;
};

struct CollectionObject : public ValueObject {
};

//
// List objects
//

struct ListStart : public CollectionObject, public Pooled<ListStart> {
  virtual auto type() const -> Type override {
    return Object::ListStart;
  }
  virtual auto name() const -> const char* override {
    return "ListStart";
  }
  virtual auto clone() const -> Object* override {
    return new ListStart();
  }
};

struct ListEnd : public CollectionObject, public Pooled<ListEnd> {
  virtual auto type() const -> Type override {
    return Object::ListEnd;
  }
  virtual auto name() const -> const char* override {
    return "ListEnd";
  }
  virtual auto clone() const -> Object* override {
    return new ListEnd();
  }
};

//
// Map objects
//

struct MapStart : public CollectionObject, public Pooled<MapStart> {
  virtual auto type() const -> Type override {
    return Object::MapStart;
  }
  virtual auto name() const -> const char* override {
    return "MapStart";
  }
  virtual auto clone() const -> Object* override {
    return new MapStart();
  }
};

struct MapKey : public CollectionObject, public Pooled<MapKey> {
  std::string key;
  MapKey(const std::string &key) { this->key = key; }
  virtual auto type() const -> Type override {
    return Object::MapKey;
  }
  virtual auto name() const -> const char* override {
    return "MapKey";
  }
  virtual auto clone() const -> Object* override {
    return new MapKey(key);
  }
};

struct MapEnd : public CollectionObject, public Pooled<MapEnd> {
  virtual auto type() const -> Type override {
    return Object::MapEnd;
  }
  virtual auto name() const -> const char* override {
    return "MapEnd";
  }
  virtual auto clone() const -> Object* override {
    return new MapEnd();
  }
};

//
// Primitive value objects
//

struct NullValue : public PrimitiveObject, public Pooled<NullValue> {
  virtual auto type() const -> Type override {
    return Object::NullValue;
  }

  virtual auto name() const -> const char* override {
    return "NullValue";
  }

  virtual auto clone() const -> Object* override {
    return new NullValue();
  }

  virtual auto to_string() const -> std::string override {
    return "null";
  }
};

struct BoolValue : public PrimitiveObject, public Pooled<BoolValue> {
  bool value;

  BoolValue(bool value) { this->value = value; }

  virtual auto type() const -> Type override {
    return Object::BoolValue;
  }

  virtual auto name() const -> const char* override {
    return "BoolValue";
  }

  virtual auto clone() const -> Object* override {
    return new BoolValue(value);
  }

  virtual auto to_string() const -> std::string override {
    return value ? "true" : "false";
  }
};

struct IntValue : public PrimitiveObject, public Pooled<IntValue> {
  int value;

  IntValue(int value) { this->value = value; }

  virtual auto type() const -> Type override {
    return Object::IntValue;
  }

  virtual auto name() const -> const char* override {
    return "IntValue";
  }

  virtual auto clone() const -> Object* override {
    return new IntValue(value);
  }

  virtual auto to_string() const -> std::string override {
    return std::to_string(value);
  }
};

struct LongValue : public PrimitiveObject, public Pooled<LongValue> {
  long long value;

  LongValue(long long value) { this->value = value; }

  virtual auto type() const -> Type override {
    return Object::LongValue;
  }

  virtual auto name() const -> const char* override {
    return "LongValue";
  }

  virtual auto clone() const -> Object* override {
    return new LongValue(value);
  }

  virtual auto to_string() const -> std::string override {
    return std::to_string(value);
  }
};

struct DoubleValue : public PrimitiveObject, public Pooled<DoubleValue> {
  double value;

  DoubleValue(double value) { this->value = value; }

  virtual auto type() const -> Type override {
    return Object::DoubleValue;
  }

  virtual auto name() const -> const char* override {
    return "DoubleValue";
  }

  virtual auto clone() const -> Object* override {
    return new DoubleValue(value);
  }

  virtual auto to_string() const -> std::string override {
    return std::to_string(value);
  }
};

struct StringValue : public PrimitiveObject, public Pooled<StringValue> {
  std::string value;

  StringValue(const std::string &value) { this->value = value; }

  virtual auto type() const -> Type override {
    return Object::StringValue;
  }

  virtual auto name() const -> const char* override {
    return "StringValue";
  }

  virtual auto clone() const -> Object* override {
    return new StringValue(value);
  }

  virtual auto to_string() const -> std::string override {
    return value;
  }
};

NS_END

#endif // OBJECT_HPP
