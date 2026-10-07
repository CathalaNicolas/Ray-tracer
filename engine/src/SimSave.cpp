#include "SimSave.hpp"

#include "Scene.hpp"

#include <sqlite3.h>

#include <string>
#include <utility>

namespace
{

std::string pathUtf8(const std::filesystem::path &path)
{
    const std::u8string u8 = path.u8string();
    return std::string(u8.begin(), u8.end());
}

struct Sqlite
{
    sqlite3 *db = nullptr;

    ~Sqlite()
    {
        if (db != nullptr)
            sqlite3_close(db);
    }

    Sqlite() = default;
    Sqlite(const Sqlite &) = delete;
    Sqlite &operator=(const Sqlite &) = delete;
};

struct Stmt
{
    sqlite3_stmt *stmt = nullptr;

    ~Stmt()
    {
        if (stmt != nullptr)
            sqlite3_finalize(stmt);
    }

    Stmt() = default;
    Stmt(const Stmt &) = delete;
    Stmt &operator=(const Stmt &) = delete;
};

bool execSql(sqlite3 *db, const char *sql)
{
    char *err = nullptr;
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &err);
    sqlite3_free(err);
    return rc == SQLITE_OK;
}

bool bindBlob(sqlite3_stmt *stmt, int index, const std::vector<std::uint8_t> &bytes)
{
    const int rc = sqlite3_bind_blob(stmt, index, bytes.data(), static_cast<int>(bytes.size()), SQLITE_TRANSIENT);
    return rc == SQLITE_OK;
}

bool readBlob(sqlite3_stmt *stmt, int index, std::vector<std::uint8_t> &out)
{
    const void *data = sqlite3_column_blob(stmt, index);
    const int size = sqlite3_column_bytes(stmt, index);
    if (size < 0)
        return false;
    if (size == 0)
    {
        out.clear();
        return true;
    }
    if (data == nullptr)
        return false;
    const auto *bytes = static_cast<const std::uint8_t *>(data);
    out.assign(bytes, bytes + size);
    return true;
}

bool openDb(const std::filesystem::path &path, int flags, Sqlite &out)
{
    const std::string utf8 = pathUtf8(path);
    const int rc = sqlite3_open_v2(utf8.c_str(), &out.db, flags, nullptr);
    return rc == SQLITE_OK && out.db != nullptr;
}

bool prepareDb(sqlite3 *db)
{
    return execSql(db, "PRAGMA journal_mode=WAL;") && execSql(db, "PRAGMA synchronous=FULL;");
}

bool createSchema(sqlite3 *db)
{
    return execSql(db,
        "CREATE TABLE IF NOT EXISTS meta ("
        "  key TEXT PRIMARY KEY NOT NULL,"
        "  value TEXT NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS world ("
        "  id INTEGER PRIMARY KEY CHECK (id = 0),"
        "  tick INTEGER NOT NULL,"
        "  seed INTEGER NOT NULL,"
        "  sim_play BLOB NOT NULL,"
        "  commands BLOB NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS entity ("
        "  entity_id INTEGER PRIMARY KEY NOT NULL,"
        "  blob BLOB NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS character ("
        "  entity_id INTEGER PRIMARY KEY NOT NULL,"
        "  blob BLOB NOT NULL"
        ");");
}

bool upsertMeta(sqlite3 *db, const char *key, const std::string &value)
{
    Stmt stmt;
    if (sqlite3_prepare_v2(db, "INSERT OR REPLACE INTO meta(key, value) VALUES(?1, ?2);", -1, &stmt.stmt, nullptr)
        != SQLITE_OK)
        return false;
    if (sqlite3_bind_text(stmt.stmt, 1, key, -1, SQLITE_STATIC) != SQLITE_OK)
        return false;
    if (sqlite3_bind_text(stmt.stmt, 2, value.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK)
        return false;
    return sqlite3_step(stmt.stmt) == SQLITE_DONE;
}

bool writeEntityRow(sqlite3 *db, const char *sql, EntityId id, const std::vector<std::uint8_t> &blob)
{
    Stmt stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt.stmt, nullptr) != SQLITE_OK)
        return false;
    if (sqlite3_bind_int64(stmt.stmt, 1, static_cast<sqlite3_int64>(id)) != SQLITE_OK)
        return false;
    if (!bindBlob(stmt.stmt, 2, blob))
        return false;
    return sqlite3_step(stmt.stmt) == SQLITE_DONE;
}

} // namespace

bool savePlaySession(const std::filesystem::path &path, const Scene &scene, const PlayState &state, SimTick tick,
    const Vec3 &lastLook, const std::vector<Command> &commands)
{
    const SimPlayBlob blob = captureSimPlay(scene, state, tick, lastLook);
    std::vector<std::uint8_t> playBytes;
    std::vector<std::uint8_t> commandBytes;
    if (!encodeSimPlayBlob(blob, playBytes) || !encodeCommandList(commands, commandBytes))
        return false;

    Sqlite sqlite;
    if (!openDb(path, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, sqlite))
        return false;
    if (!prepareDb(sqlite.db) || !createSchema(sqlite.db))
        return false;
    if (!execSql(sqlite.db, "BEGIN IMMEDIATE;"))
        return false;

    bool ok = upsertMeta(sqlite.db, "format_version", std::to_string(kPlaySessionDbVersion))
        && upsertMeta(sqlite.db, "sim_play_bitsery", std::to_string(kSimPlayBitseryVersion));
    if (ok)
    {
        Stmt world;
        ok = sqlite3_prepare_v2(sqlite.db,
                 "INSERT OR REPLACE INTO world(id, tick, seed, sim_play, commands) VALUES(0, ?1, ?2, ?3, ?4);", -1,
                 &world.stmt, nullptr)
            == SQLITE_OK;
        ok = ok && sqlite3_bind_int64(world.stmt, 1, static_cast<sqlite3_int64>(tick)) == SQLITE_OK;
        ok = ok && sqlite3_bind_int64(world.stmt, 2, static_cast<sqlite3_int64>(blob.simSeed)) == SQLITE_OK;
        ok = ok && bindBlob(world.stmt, 3, playBytes);
        ok = ok && bindBlob(world.stmt, 4, commandBytes);
        ok = ok && sqlite3_step(world.stmt) == SQLITE_DONE;
    }
    ok = ok && execSql(sqlite.db, "DELETE FROM entity;") && execSql(sqlite.db, "DELETE FROM character;");
    for (const SimEntityRecord &record : blob.entities)
    {
        if (!ok)
            break;
        std::vector<std::uint8_t> entityBytes;
        ok = encodeSimEntityRecord(record, entityBytes)
            && writeEntityRow(sqlite.db, "INSERT INTO entity(entity_id, blob) VALUES(?1, ?2);", record.id, entityBytes);
        if (ok && record.id == blob.playerId && blob.playerId != kInvalidEntityId)
        {
            ok = writeEntityRow(sqlite.db, "INSERT INTO character(entity_id, blob) VALUES(?1, ?2);", record.id,
                entityBytes);
        }
    }

    if (!ok)
    {
        execSql(sqlite.db, "ROLLBACK;");
        return false;
    }
    if (!execSql(sqlite.db, "COMMIT;"))
    {
        execSql(sqlite.db, "ROLLBACK;");
        return false;
    }
    sqlite3_wal_checkpoint_v2(sqlite.db, nullptr, SQLITE_CHECKPOINT_PASSIVE, nullptr, nullptr);
    return true;
}

bool loadPlaySession(const std::filesystem::path &path, Scene &scene, PlayState &state, PlaySessionInfo &info)
{
    Sqlite sqlite;
    if (!openDb(path, SQLITE_OPEN_READWRITE, sqlite))
        return false;

    Stmt meta;
    if (sqlite3_prepare_v2(sqlite.db, "SELECT value FROM meta WHERE key = 'format_version';", -1, &meta.stmt, nullptr)
        != SQLITE_OK)
        return false;
    if (sqlite3_step(meta.stmt) != SQLITE_ROW)
        return false;
    const unsigned char *versionText = sqlite3_column_text(meta.stmt, 0);
    if (versionText == nullptr || std::string(reinterpret_cast<const char *>(versionText)) != std::to_string(kPlaySessionDbVersion))
        return false;

    Stmt world;
    if (sqlite3_prepare_v2(sqlite.db, "SELECT tick, seed, sim_play, commands FROM world WHERE id = 0;", -1, &world.stmt,
            nullptr)
        != SQLITE_OK)
        return false;
    if (sqlite3_step(world.stmt) != SQLITE_ROW)
        return false;

    std::vector<std::uint8_t> playBytes;
    std::vector<std::uint8_t> commandBytes;
    if (!readBlob(world.stmt, 2, playBytes) || !readBlob(world.stmt, 3, commandBytes))
        return false;

    SimPlayBlob blob;
    if (!decodeSimPlayBlob(playBytes, blob))
        return false;
    if (!decodeCommandList(commandBytes, info.commands))
        return false;
    if (!applySimPlay(scene, state, blob))
        return false;

    info.tick = blob.tick;
    info.lastLook = blob.lastLook;
    info.seed = blob.simSeed;
    return true;
}
