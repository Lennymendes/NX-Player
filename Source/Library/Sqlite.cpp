// NX Player — MIT License
#include "Sqlite.h"
#include <sqlite3.h>

namespace nx
{

SqliteStatement::SqliteStatement (SqliteDb& d, const char* sql) : owner (&d)
{
    if (d.handle() != nullptr)
        sqlite3_prepare_v2 (d.handle(), sql, -1, &stmt, nullptr);
    jassert (stmt != nullptr);
}

SqliteStatement::~SqliteStatement() { if (stmt) sqlite3_finalize (stmt); }

SqliteStatement::SqliteStatement (SqliteStatement&& o) noexcept : owner (o.owner), stmt (o.stmt) { o.stmt = nullptr; }

SqliteStatement& SqliteStatement::bind (int i, juce::int64 v)        { if (stmt) sqlite3_bind_int64 (stmt, i, v); return *this; }
SqliteStatement& SqliteStatement::bind (int i, double v)             { if (stmt) sqlite3_bind_double (stmt, i, v); return *this; }
SqliteStatement& SqliteStatement::bindNull (int i)                   { if (stmt) sqlite3_bind_null (stmt, i); return *this; }
SqliteStatement& SqliteStatement::bind (int i, const juce::String& v)
{
    if (stmt) sqlite3_bind_text (stmt, i, v.toRawUTF8(), -1, SQLITE_TRANSIENT);
    return *this;
}

bool SqliteStatement::step()
{
    if (stmt == nullptr) return false;
    return sqlite3_step (stmt) == SQLITE_ROW;
}

bool SqliteStatement::execute()
{
    if (stmt == nullptr) return false;
    const int rc = sqlite3_step (stmt);
    sqlite3_reset (stmt);
    return rc == SQLITE_DONE || rc == SQLITE_ROW;
}

void SqliteStatement::reset()
{
    if (stmt) { sqlite3_reset (stmt); sqlite3_clear_bindings (stmt); }
}

juce::int64 SqliteStatement::getInt64 (int c) const { return stmt ? sqlite3_column_int64 (stmt, c) : 0; }
double SqliteStatement::getDouble (int c) const     { return stmt ? sqlite3_column_double (stmt, c) : 0.0; }
bool SqliteStatement::isNull (int c) const          { return stmt == nullptr || sqlite3_column_type (stmt, c) == SQLITE_NULL; }
juce::String SqliteStatement::getText (int c) const
{
    if (stmt == nullptr) return {};
    auto* t = reinterpret_cast<const char*> (sqlite3_column_text (stmt, c));
    return t != nullptr ? juce::String::fromUTF8 (t) : juce::String();
}

//==============================================================================
SqliteDb::~SqliteDb() { close(); }

bool SqliteDb::open (const juce::File& file, juce::String& error)
{
    close();
    file.getParentDirectory().createDirectory();
    if (sqlite3_open_v2 (file.getFullPathName().toRawUTF8(), &db,
                         SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK)
    {
        error = db ? juce::String::fromUTF8 (sqlite3_errmsg (db)) : juce::String ("sqlite3_open");
        close();
        return false;
    }
    exec ("PRAGMA journal_mode=WAL");
    exec ("PRAGMA synchronous=NORMAL");
    exec ("PRAGMA foreign_keys=ON");
    sqlite3_busy_timeout (db, 5000);
    return true;
}

bool SqliteDb::openInMemory (juce::String& error)
{
    close();
    if (sqlite3_open (":memory:", &db) != SQLITE_OK) { error = "sqlite3_open :memory:"; close(); return false; }
    return true;
}

void SqliteDb::close()
{
    if (db != nullptr) sqlite3_close_v2 (db);
    db = nullptr;
}

bool SqliteDb::exec (const char* sql)
{
    std::lock_guard<std::recursive_mutex> l (mutex);
    return db != nullptr && sqlite3_exec (db, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
}

juce::String SqliteDb::lastError() const     { return db ? juce::String::fromUTF8 (sqlite3_errmsg (db)) : juce::String(); }
juce::int64 SqliteDb::lastInsertId() const   { return db ? sqlite3_last_insert_rowid (db) : 0; }
int SqliteDb::changes() const                { return db ? sqlite3_changes (db) : 0; }

} // namespace nx
