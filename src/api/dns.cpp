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

#include <cstring>

#include "dns.hpp"
#include "net.hpp"

namespace pipy {

// DNS Header
thread_local static pjs::ConstStr STR_id("id");
thread_local static pjs::ConstStr STR_qr("qr");
thread_local static pjs::ConstStr STR_opcode("opcode");
thread_local static pjs::ConstStr STR_aa("aa");
thread_local static pjs::ConstStr STR_tc("tc");
thread_local static pjs::ConstStr STR_rd("rd");
thread_local static pjs::ConstStr STR_ra("ra");
thread_local static pjs::ConstStr STR_zero("zero");
thread_local static pjs::ConstStr STR_rcode("rcode");
thread_local static pjs::ConstStr STR_question("question");
thread_local static pjs::ConstStr STR_answer("answer");
thread_local static pjs::ConstStr STR_authority("authority");
thread_local static pjs::ConstStr STR_additional("additional");

// DNS Record
thread_local static pjs::ConstStr STR_name("name");
thread_local static pjs::ConstStr STR_type("type");
thread_local static pjs::ConstStr STR_class("class");
thread_local static pjs::ConstStr STR_ttl("ttl");
thread_local static pjs::ConstStr STR_rdlength("rdlength");
thread_local static pjs::ConstStr STR_rdata("rdata");

// SOA Data
thread_local static pjs::ConstStr STR_mname("mname");
thread_local static pjs::ConstStr STR_rname("rname");
thread_local static pjs::ConstStr STR_serial("serial");
thread_local static pjs::ConstStr STR_refresh("refresh");
thread_local static pjs::ConstStr STR_retry("retry");
thread_local static pjs::ConstStr STR_expire("expire");
thread_local static pjs::ConstStr STR_minimum("minimum");

// SRV Data
thread_local static pjs::ConstStr STR_priority("priority");
thread_local static pjs::ConstStr STR_weight("weight");
thread_local static pjs::ConstStr STR_port("port");
thread_local static pjs::ConstStr STR_target("target");

//  MX Data
thread_local static pjs::ConstStr STR_preference("preference");
thread_local static pjs::ConstStr STR_exchange("exchange");

// clang-format off

enum class RecordType {
  TYPE_A     = 1,
  TYPE_NS    = 2,
  TYPE_CNAME = 5,
  TYPE_SOA   = 6,
  TYPE_PTR   = 12,
  TYPE_MX    = 15,
  TYPE_TXT   = 16,
  TYPE_AAAA  = 28,
  TYPE_SRV   = 33,
  TYPE_OPT   = 41
};

static const unsigned int DNS_IN_CLASS      = 1;
static const unsigned int MIN_PACKET_LENGTH = 12;
static const unsigned int DNS_MAX_LABELLEN  = 63;
static const unsigned int DNS_MAX_DOMAINLEN = 255;

// clang-format on

thread_local static Data::Producer s_dp("DNS");

static int push_int8(Data::Builder &db, int value) {
  db.push(uint8_t(value));
  return 1;
}

static int push_int16(Data::Builder &db, int value) {
  db.push(uint8_t(value >> 8));
  db.push(uint8_t(value >> 0));
  return 2;
}

static int push_int32(Data::Builder &db, int value) {
  db.push(uint8_t(value >> 24));
  db.push(uint8_t(value >> 16));
  db.push(uint8_t(value >>  8));
  db.push(uint8_t(value >>  0));
  return 4;
}

static int read_name(const uint8_t *buf, const uint8_t *buf_end, uint8_t *place, uint8_t name[], const int size, int *length) {
  int skip = 0;
  uint8_t *ptr = place;
  unsigned int pos = 0, jumped = 0, offset;

  while (ptr < buf_end && *ptr != 0) {
    if (*ptr >= 0b11000000) {
      if (ptr + 1 == buf_end || jumped == 1) {
        return -1;
      }
      offset = (*ptr & 0b00111111) << 8 | *(ptr + 1);
      if (skip == 0) {
        skip = ptr - place + 2;
      }
      ptr = (uint8_t *)buf + offset;
      if (ptr >= buf_end) {
        return -2;
      }
      jumped = 1;
      continue;
    }
    if (*ptr > DNS_MAX_LABELLEN || pos + *ptr + 2 >= size) {
      return -3;
    }
    if (pos > 0) {
      name[pos++] = '.';
    }
    std::memcpy(&name[pos], ptr + 1, *ptr);
    pos += *ptr;
    ptr += *ptr + 1;
    jumped = 0;
  }
  name[pos] = '\0';
  *length = pos;
  return skip > 0 ? skip : ptr + 1 - place;
}

static int write_name(uint8_t *place, uint8_t name[], const int size) {
  uint8_t *ptr = name;
  uint8_t *ps = place;
  uint8_t *pe = place;

  while (*pe != '\0') {
    if (*pe == '.') {
      if (pe == ps) {
        return -1;
      }
      int n = pe - ps;
      if (ptr - name + n + 2 > size) {
        return -2;
      }
      *ptr++ = n;
      std::memcpy(ptr, ps, n);
      ptr += n;
      ps = ++pe;
    } else {
      ++pe;
    }
  }
  if (pe > ps) {
    int n = pe - ps;
    if (ptr - name + n + 2 > size) {
      return -3;
    }
    *ptr++ = n;
    std::memcpy(ptr, ps, n);
    ptr += n;
  }
  *ptr++ = '\0';
  return ptr - name;
}

static int read_soa(const uint8_t *buf, const uint8_t *buf_end, uint8_t *place, pjs::Object *soa) {
  uint8_t *ptr = place;
  uint8_t name[DNS_MAX_DOMAINLEN];

  int len = 0;
  int num = read_name(buf, buf_end, ptr, name, sizeof(name), &len);
  if (num < 1) {
    throw std::runtime_error("dns decode # soa mname error");
  }
  soa->set(STR_mname, pjs::Str::make((char *)name, len));
  ptr += num;

  len = 0;
  num = read_name(buf, buf_end, ptr, name, sizeof(name), &len);
  if (num < 1) {
    throw std::runtime_error("dns decode # soa rname error");
  }
  soa->set(STR_rname, pjs::Str::make((char *)name, len));
  ptr += num;

  if (ptr + 20 > buf_end) {
    throw std::runtime_error("dns decode # soa error");
  }
  soa->set(STR_serial, (uint32_t)*ptr << 24 | *(ptr + 1) << 16 | *(ptr + 2) << 8 | *(ptr + 3));
  ptr += 4;
  soa->set(STR_refresh, (uint32_t)*ptr << 24 | *(ptr + 1) << 16 | *(ptr + 2) << 8 | *(ptr + 3));
  ptr += 4;
  soa->set(STR_retry, (uint32_t)*ptr << 24 | *(ptr + 1) << 16 | *(ptr + 2) << 8 | *(ptr + 3));
  ptr += 4;
  soa->set(STR_expire, (uint32_t)*ptr << 24 | *(ptr + 1) << 16 | *(ptr + 2) << 8 | *(ptr + 3));
  ptr += 4;
  soa->set(STR_minimum, (uint32_t)*ptr << 24 | *(ptr + 1) << 16 | *(ptr + 2) << 8 | *(ptr + 3));
  ptr += 4;

  return ptr - place;
}

static int read_srv(const uint8_t *buf, const uint8_t *buf_end, uint8_t *place, pjs::Object *srv) {
  uint8_t *ptr = place;
  uint8_t name[DNS_MAX_DOMAINLEN];

  if (ptr + 6 > buf_end) {
    throw std::runtime_error("dns decode # srv error");
  }
  srv->set(STR_priority, *ptr << 8 | *(ptr + 1));
  ptr += 2;
  srv->set(STR_weight, *ptr << 8 | *(ptr + 1));
  ptr += 2;
  srv->set(STR_port, *ptr << 8 | *(ptr + 1));
  ptr += 2;

  int len = 0;
  int num = read_name(buf, buf_end, ptr, name, sizeof(name), &len);
  if (num < 1) {
    throw std::runtime_error("dns decode # srv target error");
  }
  srv->set(STR_target, pjs::Str::make((char *)name, len));
  ptr += num;

  return ptr - place;
}

static int read_mx(const uint8_t *buf, const uint8_t *buf_end, uint8_t *place, pjs::Object *mx) {
  uint8_t *ptr = place;
  uint8_t name[DNS_MAX_DOMAINLEN];

  if (ptr + 2 > buf_end) {
    throw std::runtime_error("dns decode # mx error");
  }
  mx->set(STR_preference, *ptr << 8 | *(ptr + 1));
  ptr += 2;

  int len = 0;
  int num = read_name(buf, buf_end, ptr, name, sizeof(name), &len);
  if (num < 1) {
    throw std::runtime_error("dns decode # srv exchange error");
  }
  mx->set(STR_exchange, pjs::Str::make((char *)name, len));
  ptr += num;

  return ptr - place;
}

static double get_number(pjs::Object *dns, pjs::ConstStr &key, int default_value = -1) {
  pjs::Value value;
  dns->get(key, value);
  if (value.is_number()) {
    return value.n();
  }
  return default_value;
}

static void set_number(pjs::Object *dns, pjs::ConstStr &name, int number, int default_value = -1) {
  if (number != default_value) {
    dns->set(name, number);
  }
}

static const pjs::Str *get_str(pjs::Object *dns, pjs::ConstStr &key = STR_rdata) {
  pjs::Value value;
  dns->get(key, value);
  if (value.is_string()) {
    return value.s();
  }
  return nullptr;
}

static const char *get_c_string(pjs::Object *dns, pjs::ConstStr &key = STR_rdata) {
  if (const pjs::Str *str = get_str(dns, key)) {
    return str->c_str();
  }
  return nullptr;
}

static const std::string *get_string(pjs::Object *dns, pjs::ConstStr &key = STR_rdata) {
  if (const pjs::Str *str = get_str(dns, key)) {
    return &str->str();
  }
  return nullptr;
}

static int get_type(pjs::Object *dns) {
  pjs::Value value;

  dns->get(STR_type, value);
  if (value.is_string()) {
    auto result = pjs::EnumDef<RecordType>::value(value.s());

    if (int(result) > 0) {
      return int(result);
    }
  } else if (value.is_number()) {
    return value.n();
  }

  return -1;
}

static void set_type(pjs::Object *dns, uint16_t type) {
  auto result = pjs::EnumDef<RecordType>::name(RecordType(type));
  if (result) {
    dns->set(STR_type, pjs::Str::make(result->str()));
  } else {
    dns->set(STR_type, type);
  }
}

static int read_question(const uint8_t *buf, const uint8_t *buf_end, uint8_t *place, pjs::Array *array) {
  uint8_t *ptr = place;
  uint8_t name[DNS_MAX_DOMAINLEN];

  int len = 0;
  int num = read_name(buf, buf_end, ptr, name, sizeof(name), &len);
  if (num < 1 || ptr + num + 4 > buf_end) {
    throw std::runtime_error("dns decode # question error");
  }

  pjs::Ref<pjs::Object> question(pjs::Object::make());

  question->set(STR_name, pjs::Str::make((char *)name, len));
  ptr += num;
  set_type(question, *ptr << 8 | *(ptr + 1));
  ptr += 2;
  set_number(question, STR_class, *ptr << 8 | *(ptr + 1), DNS_IN_CLASS);
  ptr += 2;
  array->push(question.get());

  return ptr - place;
}

static int assign_type(const uint8_t *place, uint16_t *type, pjs::Object *dns) {
  *type = *place << 8 | *(place + 1);
  auto result = pjs::EnumDef<RecordType>::name(RecordType(*type));

  if (result) {
    dns->set(STR_type, pjs::Str::make(result->str()));
  } else {
    dns->set(STR_type, *type);
  }
  return 2;
}

static int assign_class(const uint8_t *place, pjs::Object *dns) {
  uint16_t clazz = *place << 8 | *(place + 1);

  if (clazz != DNS_IN_CLASS) {
    dns->set(STR_class, clazz);
  }
  return 2;
}

static int read_record(const uint8_t *buf, const uint8_t *buf_end, uint8_t *place, pjs::Array *array) {
  uint8_t *ptr = place;
  uint8_t name[DNS_MAX_DOMAINLEN];

  int len = 0, num;
  num = read_name(buf, buf_end, ptr, name, sizeof(name), &len);
  if (num < 1 || (ptr + num + 10 > buf_end)) {
    throw std::runtime_error("dns decode # record name error");
  }

  pjs::Ref<pjs::Object> record(pjs::Object::make());

  record->set(STR_name, pjs::Str::make((char *)name, len));
  ptr += num;
  uint16_t type = 0;
  ptr += assign_type(ptr, &type, record);
  ptr += assign_class(ptr, record);
  record->set(STR_ttl, (uint32_t)*ptr << 24 | *(ptr + 1) << 16 | *(ptr + 2) << 8 | *(ptr + 3));
  ptr += 4;
  int rdlength = *ptr << 8 | *(ptr + 1);
  ptr += 2;
  if (ptr + rdlength > buf_end) {
    throw std::runtime_error("dns decode # rdlength error");
  }

  if (type == int(RecordType::TYPE_A)) {
    if (rdlength != 4) {
      throw std::runtime_error("dns decode # A rdata error");
    }
    char ips[20];
    sprintf(ips, "%d.%d.%d.%d", *ptr, *(ptr + 1), *(ptr + 2), *(ptr + 3));
    record->set(STR_rdata, pjs::Str::make(ips));
  } else if (type == int(RecordType::TYPE_AAAA)) {
    if (rdlength != 16) {
      throw std::runtime_error("dns decode # AAAA rdata error");
    }
    Data data(ptr, rdlength, &s_dp);
    record->set(STR_rdata, pjs::Str::make(data.to_string(Data::Encoding::hex)));
  } else if (type == int(RecordType::TYPE_SOA)) {
    auto soa = pjs::Ref<pjs::Object>(pjs::Object::make());
    int num = read_soa(buf, buf_end, ptr, soa);
    if (num < 1) {
      throw std::runtime_error("dns decode # failed to read SOA");
    }
    record->set(STR_rdata, soa.get());
  } else if (type == int(RecordType::TYPE_SRV)) {
    auto srv = pjs::Ref<pjs::Object>(pjs::Object::make());
    int num = read_srv(buf, buf_end, ptr, srv);
    if (num < 1) {
      throw std::runtime_error("dns decode # failed to read SRV");
    }
    record->set(STR_rdata, srv.get());
  } else if (type == int(RecordType::TYPE_MX)) {
    auto mx = pjs::Ref<pjs::Object>(pjs::Object::make());
    int num = read_mx(buf, buf_end, ptr, mx);
    if (num < 1) {
      throw std::runtime_error("dns decode # failed to read MX");
    }
    record->set(STR_rdata, mx.get());
  } else if (type == int(RecordType::TYPE_PTR)) {
    int len = 0;
    int num = read_name(buf, buf_end, ptr, name, sizeof(name), &len);
    if (num < 1) {
      throw std::runtime_error("dns decode # failed to read PTR");
    }
    record->set(STR_rdata, pjs::Str::make((char *)name, len));
  } else if (type == int(RecordType::TYPE_TXT)) {
    int len = *ptr;
    if (len + 1 == rdlength) {
      record->set(STR_rdata, pjs::Str::make((char *)ptr + 1, len));
    }
  } else if (type == int(RecordType::TYPE_CNAME) || type == int(RecordType::TYPE_NS)) {
    int len = 0;
    int num = read_name(buf, buf_end, ptr, name, sizeof(name), &len);
    if (num < 1) {
      throw std::runtime_error("dns decode # failed to read CNAME");
    }
    record->set(STR_rdata, pjs::Str::make((char *)name, len));
  } else { // HEX
    Data data(ptr, rdlength, &s_dp);
    record->set(STR_rdata, pjs::Str::make(data.to_string(Data::Encoding::hex)));
  }
  ptr += rdlength;
  array->push(record.get());

  return ptr - place;
}

static auto dns_decode(const uint8_t *buf, int length, pjs::Object *dns) -> pjs::Object * {
  const uint8_t *buf_end = buf + length;
  int pos = 0, qdcount, ancount, nscount, arcount;

  if (length < MIN_PACKET_LENGTH) {
    throw std::runtime_error("dns decode # data error");
  }
  dns->set(STR_id, buf[pos] << 8 | buf[pos + 1]);
  pos += 2;
  set_number(dns, STR_qr, (buf[pos] & 0b10000000) >> 7, 0);
  set_number(dns, STR_opcode, (buf[pos] & 0b01111000) >> 3, 0);
  set_number(dns, STR_aa, (buf[pos] & 0b00000100) >> 2, 0);
  set_number(dns, STR_tc, (buf[pos] & 0b00000010) >> 1, 0);
  set_number(dns, STR_rd, buf[pos++] & 0b00000001, 0);
  set_number(dns, STR_ra, (buf[pos] & 0b10000000) >> 7, 0);
  set_number(dns, STR_zero, (buf[pos] & 0b01110000) >> 4, 0);
  set_number(dns, STR_rcode, buf[pos++] & 0b00001111, 0);
  qdcount = buf[pos] << 8 | buf[pos + 1];
  pos += 2;
  ancount = buf[pos] << 8 | buf[pos + 1];
  pos += 2;
  nscount = buf[pos] << 8 | buf[pos + 1];
  pos += 2;
  arcount = buf[pos] << 8 | buf[pos + 1];
  pos += 2;

  pjs::Ref<pjs::Array> question(pjs::Array::make());
  pjs::Ref<pjs::Array> answer(pjs::Array::make());
  pjs::Ref<pjs::Array> authority(pjs::Array::make());
  pjs::Ref<pjs::Array> additional(pjs::Array::make());

  if (qdcount > 0) {
    dns->set(STR_question, question.get());
  }
  if (ancount > 0) {
    dns->set(STR_answer, answer.get());
  }
  if (nscount > 0) {
    dns->set(STR_authority, authority.get());
  }
  if (arcount > 0) {
    dns->set(STR_additional, additional.get());
  }

  uint8_t *ptr = (uint8_t *)buf + pos;

  for (int i = 0; i < qdcount; i++) {
    int num = read_question(buf, buf_end, ptr, question);
    if (num < 1) {
      throw std::runtime_error("dns decode # question error");
    }
    ptr += num;
  }
  if (question->length() != qdcount) {
    throw std::runtime_error("dns decode # question count error");
  }

  struct {
    int count;
    pjs::Array *array;
  } records[] = {{ancount, answer}, {nscount, authority}, {arcount, additional}};

  for (auto rec : records) {
    for (int i = 0; i < rec.count; i++) {
      int num = read_record(buf, buf_end, ptr, rec.array);
      if (num < 1) {
        throw std::runtime_error("dns decode # record error");
      }
      ptr += num;
    }
    if (rec.array->length() != rec.count) {
      throw std::runtime_error("dns decode # record count error");
    }
  }

  return dns;
}

static int write_soa(pjs::Object *soa, Data::Builder &db) {
  int len, skip = 0;
  uint8_t name[DNS_MAX_DOMAINLEN];

  const char *m_name = get_c_string(soa, STR_mname);
  if (!m_name || (len = write_name((uint8_t *)m_name, name, sizeof(name))) < 1) {
    throw std::runtime_error("dns encode # soa mname error");
  }
  db.push((char *)name, len);
  skip += len;

  const char *r_name = get_c_string(soa, STR_rname);
  if (!r_name || (len = write_name((uint8_t *)r_name, name, sizeof(name))) < 1) {
    throw std::runtime_error("dns encode # soa rname error");
  }
  db.push((char *)name, len);
  skip += len;

  int64_t serial = get_number(soa, STR_serial);
  if (serial < 0) {
    throw std::runtime_error("dns encode # soa serial error");
  }
  skip += push_int32(db, serial);

  int64_t refresh = get_number(soa, STR_refresh);
  if (refresh < 0) {
    throw std::runtime_error("dns encode # soa refresh error");
  }
  skip += push_int32(db, refresh);

  int64_t retry = get_number(soa, STR_retry);
  if (retry < 0) {
    throw std::runtime_error("dns encode # soa retry error");
  }
  skip += push_int32(db, retry);

  int64_t expire = get_number(soa, STR_expire);
  if (expire < 0) {
    throw std::runtime_error("dns encode # soa expire error");
  }
  skip += push_int32(db, expire);

  int64_t minimum = get_number(soa, STR_minimum);
  if (minimum < 0) {
    throw std::runtime_error("dns encode # soa minimum error");
  }
  skip += push_int32(db, minimum);

  return skip;
}

static int write_srv(pjs::Object *srv, Data::Builder &db) {
  int len, skip = 0;
  uint8_t name[DNS_MAX_DOMAINLEN];

  int priority = get_number(srv, STR_priority);
  if (priority < 0) {
    throw std::runtime_error("dns encode # srv priority error");
  }
  skip += push_int16(db, priority);

  int weight = get_number(srv, STR_weight);
  if (weight < 0) {
    throw std::runtime_error("dns encode # srv weight error");
  }
  skip += push_int16(db, weight);

  int port = get_number(srv, STR_port);
  if (port < 0) {
    throw std::runtime_error("dns encode # srv port error");
  }
  skip += push_int16(db, port);

  const char *target = get_c_string(srv, STR_target);
  if (!target || (len = write_name((uint8_t *)target, name, sizeof(name))) < 1) {
    throw std::runtime_error("dns encode # srv target error");
  }
  db.push((char *)name, len);
  skip += len;

  return skip;
}

static int write_mx(pjs::Object *mx, Data::Builder &db) {
  int len, skip = 0;
  uint8_t name[DNS_MAX_DOMAINLEN];

  int preference = get_number(mx, STR_preference);
  if (preference < 0) {
    throw std::runtime_error("dns encode # mx preference error");
  }
  skip += push_int16(db, preference);

  const char *exchange = get_c_string(mx, STR_exchange);
  if (!exchange || (len = write_name((uint8_t *)exchange, name, sizeof(name))) < 1) {
    throw std::runtime_error("dns encode # mx exchange error");
  }
  db.push((char *)name, len);
  skip += len;

  return skip;
}

static int write_question(pjs::Object *dns, Data::Builder &db) {
  int len, skip = 0;
  uint8_t name[DNS_MAX_DOMAINLEN];

  const char *q_name = get_c_string(dns, STR_name);
  if (!q_name || (len = write_name((uint8_t *)q_name, name, sizeof(name))) < 1) {
    throw std::runtime_error("dns encode # question name error");
  }
  db.push((char *)name, len);
  skip += len;

  int type = get_type(dns);
  if (type < 0) {
    throw std::runtime_error("dns encode # question type error");
  }
  skip += push_int16(db, type);

  int qclass = get_number(dns, STR_class, DNS_IN_CLASS);
  skip += push_int16(db, qclass);

  return skip;
}

static auto get_object(pjs::Object *dns, pjs::ConstStr &key = STR_rdata) -> pjs::Object * {
  pjs::Value value;

  dns->get(key, value);
  if (value.is_object()) {
    return value.o();
  }
  return nullptr;
}

static auto get_array(pjs::Object *dns, pjs::ConstStr &key) -> pjs::Array * {
  pjs::Value value;

  dns->get(key, value);
  if (value.is_array()) {
    return value.as<pjs::Array>();
  }
  return nullptr;
}

static int get_array_size(pjs::Object *dns, pjs::ConstStr &key) {
  if (pjs::Array *array = get_array(dns, key)) {
    return array->length();
  }
  return 0;
}

static int push_r_data(Data::Builder &db, pjs::Object *rdata, std::function<int(pjs::Object *soa, Data::Builder &db)> func) {
  Data data;
  Data::Builder tdb(data, &s_dp);

  int skip = 0;
  int num = func(rdata, tdb);
  if (num < 1) {
    return num;
  }
  skip += push_int16(db, num);
  tdb.flush();
  db.push(std::move(data));
  skip += num;

  return skip;
}

static int push_hex_string(Data::Builder &db, const std::string *hex_str) {
  int skip = 0;
  Data data(*hex_str, Data::Encoding::hex, &s_dp);
  skip += push_int16(db, data.size());
  skip += data.size();
  db.push(std::move(data));
  return skip;
}

static int write_record(pjs::Object *dns, Data::Builder &db) {
  int len, skip = 0;
  uint8_t name[DNS_MAX_DOMAINLEN];

  const char *d_name = get_c_string(dns, STR_name);
  if (!d_name || (len = write_name((uint8_t *)d_name, name, sizeof(name))) < 1) {
    throw std::runtime_error("dns encode # record name error");
  }
  db.push((char *)name, len);
  skip += len;

  int type = get_type(dns);
  if (type < 0) {
    throw std::runtime_error("dns encode # record type error");
  }
  skip += push_int16(db, type);

  int clazz = get_number(dns, STR_class, DNS_IN_CLASS);
  skip += push_int16(db, clazz);

  int64_t ttl = get_number(dns, STR_ttl);
  if (ttl < 0) {
    throw std::runtime_error("dns encode # record ttl error");
  }
  skip += push_int32(db, ttl);

  if (type == int(RecordType::TYPE_A)) {
    uint16_t a = 0, b = 0, c = 0, d = 0;
    const char *rdata = get_c_string(dns);
    if (!rdata) {
      throw std::runtime_error("dns encode # A rdata error");
    }
    sscanf((char *)rdata, "%hu.%hu.%hu.%hu", &a, &b, &c, &d);
    skip += push_int16(db, 4);
    skip += push_int8(db, a);
    skip += push_int8(db, b);
    skip += push_int8(db, c);
    skip += push_int8(db, d);
  } else if (type == int(RecordType::TYPE_AAAA)) {
    const std::string *rdata = get_string(dns);
    if (!rdata || rdata->length() != 32) {
      throw std::runtime_error("dns encode # AAAA rdata error");
    }
    skip += push_hex_string(db, rdata);
  } else if (type == int(RecordType::TYPE_SOA)) {
    int num = push_r_data(db, get_object(dns), write_soa);
    if (num < 1) {
      throw std::runtime_error("dns encode # failed to write SOA");
    }
    skip += num;
  } else if (type == int(RecordType::TYPE_SRV)) {
    int num = push_r_data(db, get_object(dns), write_srv);
    if (num < 1) {
      throw std::runtime_error("dns encode # failed to write SRV");
    }
    skip += num;
  } else if (type == int(RecordType::TYPE_MX)) {
    int num = push_r_data(db, get_object(dns), write_mx);
    if (num < 1) {
      throw std::runtime_error("dns encode # failed to write MX");
    }
    skip += num;
  } else if (type == int(RecordType::TYPE_PTR)) {
    int len;
    const char *ptr_str = get_c_string(dns);
    if (!ptr_str || (len = write_name((uint8_t *)ptr_str, name, sizeof(name))) < 1) {
      throw std::runtime_error("dns encode # failed to write PTR");
    }
    skip += push_int16(db, len);
    db.push((char *)name, len);
    skip += len;
  } else if (type == int(RecordType::TYPE_TXT)) {
    const std::string *txt_str = get_string(dns);
    if (!txt_str) {
      throw std::runtime_error("dns encode # TXT rdata error");
    }
    int num = txt_str->length();
    skip += push_int16(db, num + 1);
    skip += push_int8(db, num);
    db.push((char *)txt_str->c_str(), num);
    skip += num;
  } else if (type == int(RecordType::TYPE_CNAME) || type == int(RecordType::TYPE_NS)) {
    int len;
    const char *cname_str = get_c_string(dns);
    if (!cname_str || (len = write_name((uint8_t *)cname_str, name, sizeof(name))) < 1) {
      throw std::runtime_error("dns encode # failed to write CNAME");
    }
    skip += push_int16(db, len);
    db.push((char *)name, len);
    skip += len;
  } else { // HEX
    const std::string *hex_str = get_string(dns);
    if (!hex_str) {
      throw std::runtime_error("dns encode # HEX rdata error");
    }
    skip += push_hex_string(db, hex_str);
  }

  return skip;
}

static int dns_encode(pjs::Object *dns, Data::Builder &db) {
  int skip = 0;

  int id = get_number(dns, STR_id);
  if (id < 0) {
    throw std::runtime_error("dns encode # id error");
  }
  skip += push_int16(db, id);

  int qr = get_number(dns, STR_qr, 0);
  int opcode = get_number(dns, STR_opcode, 0);
  int aa = get_number(dns, STR_aa, 0);
  int tc = get_number(dns, STR_tc, 0);
  int rd = get_number(dns, STR_rd, 0);
  skip += push_int8(db, qr << 7 | opcode << 3 | aa << 2 | tc << 1 | rd);

  int ra = get_number(dns, STR_ra, 0);
  int zero = get_number(dns, STR_zero, 0);
  int rcode = get_number(dns, STR_rcode, 0);
  skip += push_int8(db, ra << 7 | zero << 4 | rcode);

  int qdcount = get_array_size(dns, STR_question);
  int ancount = get_array_size(dns, STR_answer);
  int nscount = get_array_size(dns, STR_authority);
  int arcount = get_array_size(dns, STR_additional);

  skip += push_int16(db, qdcount);
  skip += push_int16(db, ancount);
  skip += push_int16(db, nscount);
  skip += push_int16(db, arcount);

  for (int i = 0; i < qdcount; i++) {
    pjs::Value value;
    pjs::Array *array = get_array(dns, STR_question);

    array->get(i, value);
    if (!value.is_object()) {
      throw std::runtime_error("dns encode # question error");
    }
    int len = write_question(value.o(), db);
    if (len < 0) {
      throw std::runtime_error("dns encode # question error");
    }
    skip += len;
  }

  thread_local static pjs::ConstStr *records[] = { &STR_answer, &STR_authority, &STR_additional };

  for (auto rec : records) {
    pjs::Array *array = get_array(dns, *rec);

    if (!array) {
      continue;
    }
    for (int i = 0; i < array->length(); i++) {
      pjs::Value value;

      array->get(i, value);
      if (!value.is_object()) {
        throw std::runtime_error("dns encode # record error");
      }
      int len = write_record(value.o(), db);
      if (len < 0) {
        throw std::runtime_error("dns encode # record error");
      }
      skip += len;
    }
  }

  return skip;
}

//
// DNSResolver
//

class DNSResolver : public pjs::Pooled<DNSResolver> {
public:
  DNSResolver(const std::string &hostname, const std::function<void(pjs::Array*)> &cb)
    : m_resolver(Net::context())
    , m_cb(cb)
  {
    m_resolver.async_resolve(
      hostname, std::string(),
      [this](
        const std::error_code &ec,
        asio::ip::icmp::resolver::results_type results
      ) {
        if (ec) {
          m_cb(nullptr);
        } else {
          auto a = pjs::Array::make(results.size());
          int i = 0;
          for (const auto &result : results) {
            pjs::Value v(result.endpoint().address().to_string());
            a->set(i++, v);
          }
          m_cb(a);
        }
        delete this;
      }
    );
  }

private:
  asio::ip::icmp::resolver m_resolver;
  std::function<void(pjs::Array*)> m_cb;
};

//
// DNS
//

auto DNS::decode(const Data &data) -> pjs::Object * {
  auto dns = pjs::Object::make();
  try {
    auto buffer = data.to_bytes();
    return dns_decode(buffer.data(), buffer.size(), dns);
  } catch (std::runtime_error &err) {
    dns->release();
    throw err;
  }
}

void DNS::encode(pjs::Object *dns, Data::Builder &db) {
  if (!dns) {
    throw std::runtime_error("dns encode # object is null");
  }
  dns_encode(dns, db);
}

void DNS::resolve(const std::string &hostname, const std::function<void(pjs::Array*)> &cb) {
  new DNSResolver(hostname, cb);
}

} // namespace pipy

namespace pjs {

using namespace pipy;

template <> void EnumDef<RecordType>::init() {
  define(RecordType::TYPE_A, "A");
  define(RecordType::TYPE_NS, "NS");
  define(RecordType::TYPE_CNAME, "CNAME");
  define(RecordType::TYPE_SOA, "SOA");
  define(RecordType::TYPE_PTR, "PTR");
  define(RecordType::TYPE_MX, "MX");
  define(RecordType::TYPE_TXT, "TXT");
  define(RecordType::TYPE_AAAA, "AAAA");
  define(RecordType::TYPE_SRV, "SRV");
  define(RecordType::TYPE_OPT, "OPT");
}

template <> void ClassDef<DNS>::init() {
  ctor();

  method("decode", [](Context &ctx, Object *dns, Value &ret) {
    pipy::Data *data = nullptr;

    if (!ctx.arguments(1, &data) || !data) {
      return;
    }
    try {
      ret.set(DNS::decode(*data));
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  method("encode", [](Context &ctx, Object *dns, Value &ret) {
    Value value;

    if (!ctx.arguments(1, &value) || !value.is_object()) {
      return;
    }
    try {
      pipy::Data data;
      pipy::Data::Builder db(data, &s_dp);

      DNS::encode(value.o(), db);
      db.flush();
      ret.set(pipy::Data::make(std::move(data)));
    } catch (std::runtime_error &err) {
      ctx.error(err);
    }
  });

  method("resolve", [](Context &ctx, Object *dns, Value &ret) {
    Str* hostname;
    if (!ctx.arguments(1, &hostname)) return;
    auto promise = Promise::make();
    auto settler = Promise::Settler::make(promise);
    settler->retain();
    DNS::resolve(
      hostname->str(),
      [=](Array *results) {
        settler->resolve(results);
        settler->release();
      }
    );
    ret.set(promise);
  });
}

} // namespace pjs
