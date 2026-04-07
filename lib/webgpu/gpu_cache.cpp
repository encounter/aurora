#include <mutex>
#include <string>

#include "sqlite3.h"
#include "fmt/format.h"
#include "../internal.hpp"
#include "zstd.h"
#define XXH_STATIC_LINKING_ONLY
#include <xxhash.h>

namespace aurora::webgpu {
static Module Log("aurora::gpu::cache");

static sqlite3* db;
static sqlite3_stmt* load_stmt;
static sqlite3_stmt* store_stmt;
static bool cache_broken;
static std::mutex cache_mutex;
static std::vector<uint8_t> compress_buffer;

constexpr int CACHE_SCHEMA = 1;

static void init_abort() {
  cache_broken = true;
  sqlite3_close(db);
  db = nullptr;
}

static int sqlite3_exec(const char* sql) {
  return sqlite3_exec(db, sql, nullptr, nullptr, nullptr);
}

template<typename T>
static int sqlite3_exec(const char* sql, T callback, char** errmsg) {
  return sqlite3_exec(
      db, sql,
      [](void* cb, int b, char** c, char** d) -> int {
        auto& fp = *static_cast<T*>(cb);
        if constexpr (std::is_same_v<decltype(fp(0, 0, 0)), void>) {
          fp(b, c, d);
          return 0;
        } else {
          return fp(b, c, d);
        }
      },
      &callback,
      errmsg);
}

struct SqliteTransaction {
  bool need_rollback;
  explicit SqliteTransaction(bool immediate = false) : need_rollback(true) {
    const auto type = immediate ? "BEGIN IMMEDIATE" : "BEGIN";
    const auto ret = sqlite3_exec(db, type, nullptr, nullptr, nullptr);
    if (ret != SQLITE_OK) {
      Log.error("Failed to start transaction: {}", sqlite3_errmsg(db));
      need_rollback = false;
    }
  }
  ~SqliteTransaction() {
    if (need_rollback) {
      const auto ret = sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
      if (ret != SQLITE_OK) {
        Log.error("Failed to roll back transaction (uh oh?): {}", sqlite3_errmsg(db));
      }
    }
  }
  void commit() {
    const auto ret = sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr);
    if (ret != SQLITE_OK) {
      Log.error("Failed to commit transaction: {}", sqlite3_errmsg(db));
      return;
    }
    need_rollback = false;
  }
  explicit operator bool() const {
    return need_rollback;
  }
};

static int check(int ret) {
  if (ret != SQLITE_OK) {
    Log.error("SQLite operation failed: {}", sqlite3_errmsg(db));
  }

  return ret;
}

static bool ensure_schema_up_to_date() {
  SqliteTransaction tx(true);
  if (!tx) {
    Log.error("Failed to open schema check transaction", sqlite3_errmsg(db));
    return false;
  }

  auto ret = sqlite3_exec(
    db,
    "CREATE TABLE IF NOT EXISTS aurora_schema(value INTEGER);",
    nullptr,
    nullptr,
    nullptr);
  if (ret != SQLITE_OK) {
    Log.error("Failed to create schema table: {}", sqlite3_errmsg(db));
    return false;
  }

  bool match = false;
  auto cmd = fmt::format("SELECT * FROM aurora_schema WHERE value = {}", CACHE_SCHEMA);
  ret = sqlite3_exec(
    cmd.c_str(),
    [&match](int, char**, char**) {match = true;},
    nullptr);
  if (ret != SQLITE_OK) {
    Log.error("Failed to check schema table: {}", sqlite3_errmsg(db));
    return false;
  }

  if (match) {
    return true;
  }

  cmd = fmt::format(
    "DROP TABLE IF EXISTS cache;\n"
    "CREATE TABLE cache(key BLOB PRIMARY KEY NOT NULL, value BLOB NOT NULL, size INTEGER NOT NULL);\n"
    "DELETE FROM aurora_schema;"
    "INSERT INTO aurora_schema VALUES ({});", CACHE_SCHEMA);
  ret = sqlite3_exec(db, cmd.c_str(), nullptr, nullptr, nullptr);
  if (ret != SQLITE_OK) {
    Log.error("Failed to update schema: {}", sqlite3_errmsg(db));
    return false;
  }

  tx.commit();
  return true;
}

static bool cache_init_core() {
  Log.debug("SQLite version {}", sqlite3_libversion());

  std::string file = fmt::format("{}{}", g_config.configPath, "dawn_cache.db");
  Log.debug("Using dawn cache at {}", file);
  auto ret = sqlite3_open(file.c_str(), &db);
  if (ret != SQLITE_OK) {
    Log.error("Failed to open database: {}", sqlite3_errmsg(db));
    return false;
  }

  // WAL mode + NORMAL = no need for disk syncs, consistent but not durable is fine.
  ret = sqlite3_exec("PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL;");
  if (ret != SQLITE_OK) {
    Log.error("Failed to set pragmas: {}", sqlite3_errmsg(db));
    return false;
  }

  if (!ensure_schema_up_to_date()) {
    Log.error("Failed to validate schema");
    return false;
  }

  ret = sqlite3_prepare_v3(db, "SELECT value, size FROM cache WHERE key = ?", -1, SQLITE_PREPARE_PERSISTENT, &load_stmt, nullptr);
  if (ret != SQLITE_OK) {
    Log.error("Failed to prepare statement: {}", sqlite3_errmsg(db));
    return false;
  }

  ret = sqlite3_prepare_v3(db, "REPLACE INTO cache (key, value, size) VALUES (?, ?, ?)", -1, SQLITE_PREPARE_PERSISTENT, &store_stmt, nullptr);
  if (ret != SQLITE_OK) {
    Log.error("Failed to prepare statement: {}", sqlite3_errmsg(db));
    return false;
  }

  return true;
}

static bool cache_init() {
  if (cache_broken) {
    return false;
  }

  if (db) {
    return true;
  }

  if (!cache_init_core()) {
    Log.error("SQLite DB init failed");
    init_abort();
    return false;
  }

  Log.debug("SQLite cache init succeeded");

  return true;
}

size_t load_from_cache(void const* key, size_t keySize, void* value, size_t valueSize, void*) {
  std::lock_guard lock(cache_mutex);

  if (!cache_init()) {
    return 0;
  }

  SqliteTransaction tx;
  if (!tx) {
    Log.error("Failed to open load transaction");
    return 0;
  }

  const auto keyHash = XXH128(key, keySize, 0);
  check(sqlite3_bind_blob(load_stmt, 1, &keyHash, sizeof(keyHash), SQLITE_TRANSIENT));

  const auto ret = sqlite3_step(load_stmt);
  size_t foundSize;
  if (ret == SQLITE_ROW) {
    // Hit
    const auto foundPtr = sqlite3_column_blob(load_stmt, 0);
    foundSize = sqlite3_column_int64(load_stmt, 1);

    if (value && valueSize == foundSize) {
      const auto compSize = sqlite3_column_bytes(load_stmt, 0);

      const auto zstdRet = ZSTD_decompress(value, valueSize, foundPtr, compSize);
      if (ZSTD_isError(zstdRet)) {
        Log.error("zstd decompression error: {}", ZSTD_getErrorName(zstdRet));
        foundSize = 0;
      }
    }
  } else if (ret == SQLITE_DONE) {
    // Miss
    foundSize = 0;
  } else {
    Log.error("Looking up cache key failed: {}", sqlite3_errmsg(db));
    return 0;
  }

  check(sqlite3_reset(load_stmt));

  return foundSize;
}

void store_to_cache(void const* key, size_t keySize, void const* value, size_t valueSize, void*) {
  std::lock_guard lock(cache_mutex);

  if (!cache_init()) {
    return;
  }

  SqliteTransaction tx(true);
  if (!tx) {
    Log.error("Failed to open store transaction");
    return;
  }

  const auto bound = ZSTD_compressBound(valueSize);
  if (ZSTD_isError(bound)) {
    Log.error("Failed to calculate ZSTD_compressBound: {}", ZSTD_getErrorName(bound));
    return;
  }

  if (compress_buffer.size() < bound)
    compress_buffer.resize(bound);

  auto compressRet = ZSTD_compress(compress_buffer.data(), compress_buffer.size(), value, valueSize, 0);
  if (ZSTD_isError(compressRet)) {
    Log.error("ZSTD compression error: {}", ZSTD_getErrorName(compressRet));
    return;
  }

  const auto keyHash = XXH128(key, keySize, 0);
  check(sqlite3_bind_blob64(store_stmt, 1, &keyHash, sizeof(keyHash), SQLITE_TRANSIENT));
  check(sqlite3_bind_blob64(store_stmt, 2, compress_buffer.data(), compressRet, SQLITE_STATIC));
  check(sqlite3_bind_int64(store_stmt, 3, static_cast<sqlite3_int64>(valueSize)));

  const auto ret = sqlite3_step(store_stmt);
  if (ret != SQLITE_DONE) {
    // Error or something
    Log.error("Failed to insert row: {}", sqlite3_errmsg(db));
    return;
  }

  check(sqlite3_reset(store_stmt));
  check(sqlite3_bind_null(store_stmt, 2));

  tx.commit();
}

void cache_shutdown() {
  compress_buffer.clear();
  check(sqlite3_finalize(load_stmt));
  check(sqlite3_finalize(store_stmt));
  check(sqlite3_close(db));
  db = nullptr;
}

}
