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

#include "api/bpf.hpp"

#ifdef __linux__
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/bpf.h>

#define attr_size(FIELD) \
  (offsetof(bpf_attr, FIELD) + sizeof(bpf_attr::FIELD))

#endif // __linux__

namespace pipy {
namespace bpf {

thread_local static Data::Producer s_dp("BPF");

#ifdef __linux__

static inline int syscall_bpf(enum bpf_cmd cmd, union bpf_attr *attr, unsigned int size) {
	return syscall(__NR_bpf, cmd, attr, size);
}

static inline int syscall_bpf(enum bpf_cmd cmd, union bpf_attr *attr, unsigned int size,
  const std::function<void(union bpf_attr &attr)> &in
) {
  std::memset(attr, 0, size);
  in(*attr);
  return syscall_bpf(cmd, attr, size);
}

#endif // __linux__

//
// Map
//

Map::Map(int fd, CStruct *key_type, CStruct *value_type)
  : m_fd(fd)
  , m_key_type(key_type)
  , m_value_type(value_type)
{
#ifdef __linux__
  union bpf_attr attr;
  struct bpf_map_info info = {};
  syscall_bpf(
    BPF_OBJ_GET_INFO_BY_FD, &attr, attr_size(info),
    [&](union bpf_attr &attr) {
      attr.info.bpf_fd = fd;
      attr.info.info_len = sizeof(info);
      attr.info.info = (uint64_t)&info;
    }
  );
  m_key_size = info.key_size;
  m_value_size = info.value_size;
#endif // __linux__
}

auto Map::list() -> pjs::Array* {
  auto a = pjs::Array::make();
#ifdef __linux__
  union bpf_attr attr;
  unsigned int id = 0;
  for (;;) {
    if (!syscall_bpf(
      BPF_MAP_GET_NEXT_ID, &attr, attr_size(open_flags),
      [&](union bpf_attr &attr) { attr.start_id = id; }
    )) {
      id = attr.next_id;
    } else {
      break;
    }

    int fd = syscall_bpf(
      BPF_MAP_GET_FD_BY_ID, &attr, attr_size(open_flags),
      [&](union bpf_attr &attr) { attr.map_id = id; }
    );

		struct bpf_map_info info = {};
    syscall_bpf(
      BPF_OBJ_GET_INFO_BY_FD, &attr, attr_size(info),
      [&](union bpf_attr &attr) {
        attr.info.bpf_fd = fd;
        attr.info.info_len = sizeof(info);
        attr.info.info = (uint64_t)&info;
      }
    );
    ::close(fd);

    auto i = Info::make();
    i->name = pjs::Str::make(info.name);
    i->id = info.id;
    i->flags = info.map_flags;
    i->maxEntries = info.max_entries;
    i->keySize = info.key_size;
    i->valueSize = info.value_size;

    a->push(i);
  }
#endif // __linux__
  return a;
}

auto Map::open(int id, CStruct *key_type, CStruct *value_type) -> Map* {
#ifdef __linux__
  union bpf_attr attr;
  int fd = syscall_bpf(
    BPF_MAP_GET_FD_BY_ID, &attr, attr_size(open_flags),
    [&](union bpf_attr &attr) { attr.map_id = id; }
  );
  if (fd <= 0) {
    throw std::runtime_error("failed when trying to get fd by a map id");
  }
  return Map::make(fd, key_type, value_type);
#else
  return nullptr;
#endif // __linux__
}

auto Map::keys() -> pjs::Array* {
  if (!m_fd) return nullptr;
#ifdef __linux__
  uint8_t k[m_key_size];

  auto a = pjs::Array::make();
  uint8_t *p = nullptr;

  union bpf_attr attr;
  while (!syscall_bpf(
    BPF_MAP_GET_NEXT_KEY, &attr, attr_size(next_key),
    [&](union bpf_attr &attr) {
      attr.map_fd = m_fd;
      attr.key = (uintptr_t)p;
      attr.next_key = (uintptr_t)k;
    }
  )) {
    Data data(k, m_key_size, &s_dp);
    if (m_key_type) {
      a->push(m_key_type->decode(data));
    } else {
      a->push(Data::make(std::move(data)));
    }
    p = k;
  }

  return a;
#else
  return nullptr;
#endif // __linux__
}

auto Map::entries() -> pjs::Array* {
  if (!m_fd) return nullptr;
#ifdef __linux__
  uint8_t k[m_key_size];
  uint8_t v[m_value_size];

  auto a = pjs::Array::make();
  uint8_t *p = nullptr;

  union bpf_attr attr;
  while (!syscall_bpf(
    BPF_MAP_GET_NEXT_KEY, &attr, attr_size(next_key),
    [&](union bpf_attr &attr) {
      attr.map_fd = m_fd;
      attr.key = (uintptr_t)p;
      attr.next_key = (uintptr_t)k;
    }
  )) {
    if (syscall_bpf(
      BPF_MAP_LOOKUP_ELEM, &attr, attr_size(flags),
      [&](union bpf_attr &attr) {
        attr.map_fd = m_fd;
        attr.key = (uintptr_t)k;
        attr.value = (uintptr_t)v;
      }
    )) break;

    Data data_k(k, m_key_size, &s_dp);
    Data data_v(v, m_value_size, &s_dp);
    auto ent = pjs::Array::make(2);
    a->push(ent);

    if (m_key_type) {
      ent->set(0, m_key_type->decode(data_k));
    } else {
      ent->set(0, Data::make(std::move(data_k)));
    }

    if (m_value_type) {
      ent->set(1, m_value_type->decode(data_v));
    } else {
      ent->set(1, Data::make(std::move(data_v)));
    }

    p = k;
  }

  return a;
#else
  return nullptr;
#endif // __linux__
}

auto Map::lookup(pjs::Object *key) -> pjs::Object* {
  if (!key) return nullptr;

  pjs::Ref<Data> value;
  if (key->is<Data>()) {
    value = lookup_raw(key->as<Data>());
  } else if (m_key_type) {
    pjs::Ref<Data> raw_key = m_key_type->encode(key);
    value = lookup_raw(raw_key);
  }

  if (!value) return nullptr;
  if (m_value_type) {
    return m_value_type->decode(*value);
  } else {
    return value.release()->pass();
  }
}

void Map::update(pjs::Object *key, pjs::Object *value) {
  if (!key || !value) return;

  pjs::Ref<Data> k, v;

  if (key->is<Data>()) {
    k = key->as<Data>();
  } else if (m_key_type) {
    k = m_key_type->encode(key);
  }

  if (value->is<Data>()) {
    v = value->as<Data>();
  } else if (m_value_type) {
    v = m_value_type->encode(value);
  }

  update_raw(k, v);
}

void Map::remove(pjs::Object *key) {
  if (!key) return;
  if (key->is<Data>()) {
    delete_raw(key->as<Data>());
  } else if (m_key_type) {
    pjs::Ref<Data> k = m_key_type->encode(key);
    delete_raw(k);
  }
}

void Map::close() {
#ifdef __linux__
  ::close(m_fd);
#endif
  m_fd = 0;
}

auto Map::lookup_raw(Data *key) -> Data* {
  if (!m_fd) return nullptr;
#ifdef __linux__
  uint8_t k[m_key_size];
  uint8_t v[m_value_size];
  std::memset(k, 0, m_key_size);
  key->to_bytes(k, m_key_size);

  union bpf_attr attr;
  if (syscall_bpf(
    BPF_MAP_LOOKUP_ELEM, &attr, attr_size(flags),
    [&](union bpf_attr &attr) {
      attr.map_fd = m_fd;
      attr.key = (uintptr_t)k;
      attr.value = (uintptr_t)v;
    }
  )) return nullptr;

  return Data::make(&v[0], m_value_size, &s_dp);
#else
  return nullptr;
#endif // __linux__
}

void Map::update_raw(Data *key, Data *value) {
  if (!m_fd) return;
#ifdef __linux__
  uint8_t k[m_key_size];
  uint8_t v[m_value_size];
  std::memset(k, 0, m_key_size);
  std::memset(v, 0, m_value_size);
  key->to_bytes(k, m_key_size);
  value->to_bytes(v, m_value_size);

  union bpf_attr attr;
  syscall_bpf(
    BPF_MAP_UPDATE_ELEM, &attr, attr_size(flags),
    [&](union bpf_attr &attr) {
      attr.map_fd = m_fd;
      attr.key = (uintptr_t)k;
      attr.value = (uintptr_t)v;
    }
  );
#endif // __linux__
}

void Map::delete_raw(Data *key) {
  if (!m_fd) return;
#ifdef __linux__
  uint8_t k[m_key_size];
  std::memset(k, 0, m_key_size);
  key->to_bytes(k, m_key_size);

  union bpf_attr attr;
  syscall_bpf(
    BPF_MAP_DELETE_ELEM, &attr, attr_size(flags),
    [&](union bpf_attr &attr) {
      attr.map_fd = m_fd;
      attr.key = (uintptr_t)k;
    }
  );
#endif // __linux__
}

} // namespace bpf
} // namespace pipy

namespace pjs {

using namespace pipy;
using namespace pipy::bpf;

static bool linux_only(Context &ctx) {
#ifdef __linux__
  return true;
#else
  ctx.error("BPF not supported");
  return false;
#endif
}

//
// Map
//

template<> void ClassDef<bpf::Map>::init() {
  method("keys", [](Context &ctx, Object *obj, Value &ret) {
    if (linux_only(ctx)) {
      ret.set(obj->as<bpf::Map>()->keys());
    }
  });

  method("entries", [](Context &ctx, Object *obj, Value &ret) {
    if (linux_only(ctx)) {
      ret.set(obj->as<bpf::Map>()->entries());
    }
  });

  method("lookup", [](Context &ctx, Object *obj, Value &ret) {
    if (linux_only(ctx)) {
      Object *key;
      if (!ctx.arguments(1, &key)) return;
      ret.set(obj->as<bpf::Map>()->lookup(key));
    }
  });

  method("update", [](Context &ctx, Object *obj, Value &ret) {
    if (linux_only(ctx)) {
      Object *key, *value;
      if (!ctx.arguments(2, &key, &value)) return;
      obj->as<bpf::Map>()->update(key, value);
    }
  });

  method("delete", [](Context &ctx, Object *obj, Value &ret) {
    if (linux_only(ctx)) {
      Object *key;
      if (!ctx.arguments(1, &key)) return;
      obj->as<bpf::Map>()->remove(key);
    }
  });
}

template<> void ClassDef<Constructor<bpf::Map>>::init() {
  super<Function>();
  ctor();

  method("list", [](Context &ctx, Object *obj, Value &ret) {
    if (linux_only(ctx)) {
      ret.set(bpf::Map::list());
    }
  });

  method("open", [](Context &ctx, Object *obj, Value &ret) {
    if (linux_only(ctx)) {
      int id;
      CStruct *key_type = nullptr;
      CStruct *value_type = nullptr;
      if (!ctx.arguments(1, &id, &key_type, &value_type)) return;
      try {
        ret.set(bpf::Map::open(id, key_type, value_type));
      } catch (std::runtime_error &err) {
        ctx.error(err);
      }
    }
  });
}

template<> void ClassDef<BPF>::init() {
  ctor();
  variable("Map", class_of<Constructor<bpf::Map>>());
}

//
// Map::Info
//

template<> void ClassDef<bpf::Map::Info>::init() {
  field<Ref<Str>>("name", [](bpf::Map::Info *obj) { return &obj->name; });
  field<int>("id", [](bpf::Map::Info *obj) { return &obj->id; });
  field<int>("flags", [](bpf::Map::Info *obj) { return &obj->flags; });
  field<int>("maxEntries", [](bpf::Map::Info *obj) { return &obj->maxEntries; });
  field<int>("keySize", [](bpf::Map::Info *obj) { return &obj->keySize; });
  field<int>("valueSize", [](bpf::Map::Info *obj) { return &obj->valueSize; });
}

} // namespace pjs
