#include <mutex>
#include <string>

#include "sqlite3.h"
#include "fmt/format.h"
#include "../internal.hpp"

namespace aurora::webgpu {
static Module Log("aurora::gpu::cache");

static sqlite3* db;
static sqlite3_stmt* load_stmt;
static sqlite3_stmt* store_stmt;
static bool cache_broken;
static std::mutex cache_mutex;

constexpr int CACHE_SCHEMA = 1;

static void init_abort() {
  cache_broken = true;
  sqlite3_close(db);
  db = nullptr;
}

class SqliteError : public std::runtime_error {
public:
  explicit SqliteError(const std::string& str) : runtime_error(str) {
  }

  explicit SqliteError(const char* what) : runtime_error(what) {
  }

  static SqliteError ConsumeMsg(char* errmsg) {
    auto result = SqliteError(errmsg);
    sqlite3_free(errmsg);
    return result;
  }
};

static void sqlite_check(int ret) {
  if (ret != SQLITE_OK) {
    throw SqliteError(sqlite3_errmsg(db));
  }
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
  bool committed;
  explicit SqliteTransaction(bool immediate = false) : committed(false) {
    auto type = immediate ? "BEGIN IMMEDIATE" : "BEGIN";
    sqlite_check(sqlite3_exec(db, type, nullptr, nullptr, nullptr));
  }
  ~SqliteTransaction() {
    if (!committed) {
      sqlite_check(sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr));
    }
  }
  void commit() {
    sqlite_check(sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr));
    committed = true;
  }
};

static void ensure_schema_up_to_date() {
  SqliteTransaction tx(true);

  auto ret = sqlite3_exec(
    db,
    "CREATE TABLE IF NOT EXISTS aurora_schema(value INTEGER);",
    nullptr,
    nullptr,
    nullptr);
  sqlite_check(ret);

  bool match = false;
  auto cmd = fmt::format("SELECT * FROM aurora_schema WHERE value = {}", CACHE_SCHEMA);
  ret = sqlite3_exec(
    cmd.c_str(),
    [&match](int, char**, char**) {match = true;},
    nullptr);
  sqlite_check(ret);

  if (match) {
    return;
  }

  cmd = fmt::format(
    "DROP TABLE IF EXISTS cache;\n"
    "CREATE TABLE cache(key BLOB PRIMARY KEY NOT NULL, value BLOB NOT NULL);\n"
    "DELETE FROM aurora_schema;"
    "INSERT INTO aurora_schema VALUES ({});", CACHE_SCHEMA);
  ret = sqlite3_exec(db, cmd.c_str(), nullptr, nullptr, nullptr);
  sqlite_check(ret);

  tx.commit();
}

static bool cache_init() {
  if (cache_broken) {
    return false;
  }

  if (db) {
    return true;
  }

  try {
    Log.debug("SQLite version {}", sqlite3_libversion());

    std::string file = fmt::format("{}{}", g_config.configPath, "dawn_cache.db");
    Log.debug("Using dawn cache at {}", file);
    auto ret = sqlite3_open(file.c_str(), &db);
    sqlite_check(ret);

    // WAL mode + NORMAL = no need for disk syncs, consistent but not durable is fine.
    sqlite_check(sqlite3_exec("PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL;"));

    ensure_schema_up_to_date();

    sqlite_check(sqlite3_prepare_v3(db, "SELECT value FROM cache WHERE key = ?", -1, SQLITE_PREPARE_PERSISTENT, &load_stmt, nullptr));
    sqlite_check(sqlite3_prepare_v3(db, "REPLACE INTO cache (key, value) VALUES (?, ?)", -1, SQLITE_PREPARE_PERSISTENT, &store_stmt, nullptr));
  } catch (SqliteError& e) {
    Log.error("SQLite DB init failed: {}", e.what());
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

  try {
    SqliteTransaction tx;

    sqlite_check(sqlite3_bind_blob64(load_stmt, 1, key, keySize, SQLITE_STATIC));

    const auto ret = sqlite3_step(load_stmt);
    size_t foundSize;
    if (ret == SQLITE_ROW) {
      // Hit
      const auto foundPtr = sqlite3_column_blob(load_stmt, 0);
      foundSize = sqlite3_column_bytes(load_stmt, 0);

      if (value && valueSize == foundSize) {
        memcpy(value, foundPtr, foundSize);
      }

    } else if (ret == SQLITE_DONE) {
      // Miss
      foundSize = 0;
    } else {
      // Error
      sqlite_check(ret);
      abort(); // Should be unreachable, but let's make sure.
    }

    sqlite3_reset(load_stmt);
    sqlite_check(sqlite3_bind_null(load_stmt, 1));

    return foundSize;
  } catch (SqliteError& e) {
    Log.error("Read from cache failed: {}", e.what());
    return 0;
  }
}

void store_to_cache(void const* key, size_t keySize, void const* value, size_t valueSize, void*) {
  std::lock_guard lock(cache_mutex);

  if (!cache_init()) {
    return;
  }

  try {
    SqliteTransaction tx(true);

    sqlite_check(sqlite3_bind_blob64(store_stmt, 1, key, keySize, SQLITE_STATIC));
    sqlite_check(sqlite3_bind_blob64(store_stmt, 2, value, valueSize, SQLITE_STATIC));

    const auto ret = sqlite3_step(store_stmt);
    if (ret != SQLITE_DONE) {
      // Error
      sqlite_check(ret);
      abort(); // Should be unreachable, but let's make sure.
    }

    sqlite_check(sqlite3_reset(store_stmt));
    sqlite_check(sqlite3_bind_null(store_stmt, 1));
    sqlite_check(sqlite3_bind_null(store_stmt, 2));

    tx.commit();

  } catch (SqliteError& e) {
    Log.error("Store to cache failed: {}", e.what());
  }
}

void cache_shutdown() {
  sqlite3_close(db);
  db = nullptr;
}

}
