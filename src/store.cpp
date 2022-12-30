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

#include "store.hpp"

#include <leveldb/db.h>
#include <leveldb/write_batch.h>

namespace pipy {

thread_local static Data::Producer s_dp("LevelDB");

//
// MemoryStore
//

class MemoryStoreBatch;

class MemoryStore : public Store {
  virtual void keys(const std::string &base_key, std::set<std::string> &keys) override;
  virtual bool get(const std::string &key, Data &data) override;
  virtual void set(const std::string &key, const Data &data) override;
  virtual void erase(const std::string &key) override;
  virtual auto batch() -> Batch* override;
  virtual void close() override;
  virtual void dump(std::ostream &out) override;

  std::map<std::string, Data> m_records;

  friend class MemoryStoreBatch;
};

//
// MemoryStoreBatch
//

class MemoryStoreBatch : public Store::Batch {
  MemoryStoreBatch(MemoryStore *store)
    : m_store(store) {}

  virtual void set(const std::string &key, const Data &data) override;
  virtual void erase(const std::string &key) override;
  virtual void commit() override;
  virtual void cancel() override;

  MemoryStore* m_store;
  std::map<std::string, Data> m_records;
  std::set<std::string> m_deletions;

  friend class MemoryStore;
};

void MemoryStore::keys(const std::string &base_key, std::set<std::string> &keys) {
  for (auto i = m_records.lower_bound(base_key); i != m_records.end(); i++) {
    const auto &key = i->first;
    if (!utils::starts_with(key, base_key)) break;
    keys.insert(key);
  }
}

bool MemoryStore::get(const std::string &key, Data &data) {
  auto i = m_records.find(key);
  if (i == m_records.end()) return false;
  data = i->second;
  return true;
}

void MemoryStore::set(const std::string &key, const Data &data) {
  m_records[key] = data;
}

void MemoryStore::erase(const std::string &key) {
  m_records.erase(key);
}

auto MemoryStore::batch() -> Batch* {
  return new MemoryStoreBatch(this);
}

void MemoryStore::close() {
  delete this;
}

void MemoryStore::dump(std::ostream &out) {
  for (const auto &i : m_records) {
    out << '[' << i.first << "]:" << std::endl;
    out << i.second.to_string() << std::endl;
    out << std::endl;
  }
}

void MemoryStoreBatch::set(const std::string &key, const Data &data) {
  m_records[key] = data;
}

void MemoryStoreBatch::erase(const std::string &key) {
  m_deletions.insert(key);
}

void MemoryStoreBatch::commit() {
  for (const auto &key : m_deletions) m_store->erase(key);
  for (const auto &rec : m_records) m_store->set(rec.first, rec.second);
  delete this;
}

void MemoryStoreBatch::cancel() {
  delete this;
}

Store* Store::open_memory() {
  return new MemoryStore();
}

//
// LevelDBStore
//

class LevelDBStore : public Store {
public:
  LevelDBStore(const std::string &path);

private:
  virtual void keys(const std::string &base_key, std::set<std::string> &keys) override;
  virtual bool get(const std::string &key, Data &data) override;
  virtual void set(const std::string &key, const Data &data) override;
  virtual void erase(const std::string &key) override;
  virtual auto batch() -> Batch* override;
  virtual void close() override;
  virtual void dump(std::ostream &out) override;

  leveldb::DB* m_db = nullptr;
};

class LevelDBStoreBatch : public Store::Batch {
  LevelDBStoreBatch(leveldb::DB *db)
    : m_db(db) {}

  virtual void set(const std::string &key, const Data &data) override;
  virtual void erase(const std::string &key) override;
  virtual void commit() override;
  virtual void cancel() override;

  leveldb::DB* m_db;
  leveldb::WriteBatch m_batch;

  friend class LevelDBStore;
};

LevelDBStore::LevelDBStore(const std::string &path) {
  leveldb::Options options;
  options.create_if_missing = true;
  auto status = leveldb::DB::Open(options, path, &m_db);
  if (!status.ok()) throw std::runtime_error(status.ToString());
}

void LevelDBStore::keys(const std::string &base_key, std::set<std::string> &keys) {
  leveldb::Iterator* it = m_db->NewIterator(leveldb::ReadOptions());
  for (it->SeekToFirst(); it->Valid(); it->Next()) {
    auto key = it->key().ToString();
    if (utils::starts_with(key, base_key)) {
      keys.insert(key);
    }
  }
  delete it;
}

bool LevelDBStore::get(const std::string &key, Data &data) {
  std::string value;
  auto status = m_db->Get(leveldb::ReadOptions(), key, &value);
  if (status.IsNotFound()) return false;
  data.clear();
  data.push(value, &s_dp);
  return true;
}

void LevelDBStore::set(const std::string &key, const Data &data) {
  m_db->Put(leveldb::WriteOptions(), key, data.to_string());
}

void LevelDBStore::erase(const std::string &key) {
  m_db->Delete(leveldb::WriteOptions(), key);
}

auto LevelDBStore::batch() -> Batch* {
  return new LevelDBStoreBatch(m_db);
}

void LevelDBStore::close() {
  delete m_db;
  m_db = nullptr;
}

void LevelDBStore::dump(std::ostream &out) {
}

void LevelDBStoreBatch::set(const std::string &key, const Data &data) {
  m_batch.Put(key, data.to_string());
}

void LevelDBStoreBatch::erase(const std::string &key) {
  m_batch.Delete(key);
}

void LevelDBStoreBatch::commit() {
  m_db->Write(leveldb::WriteOptions(), &m_batch);
  delete this;
}

void LevelDBStoreBatch::cancel() {
  delete this;
}

Store* Store::open_level_db(const std::string &path) {
  return new LevelDBStore(path);
}

} // namespace pipy