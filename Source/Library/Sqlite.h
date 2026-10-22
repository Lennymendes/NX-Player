// NX Player — MIT License
// Thin RAII wrapper over SQLite (prepared statements, transactions, one mutex per connection).
#pragma once

#include <juce_core/juce_core.h>
#include <mutex>
#include <optional>

struct sqlite3;
struct sqlite3_stmt;

namespace nx
{

class SqliteDb;

class SqliteStatement
{
public:
    SqliteStatement (SqliteDb& db, const char* sql);
    ~SqliteStatement();
    SqliteStatement (SqliteStatement&&) noexcept;
    SqliteStatement (const SqliteStatement&) = delete;

    bool isValid() const noexcept { return stmt != nullptr; }

    SqliteStatement& bind (int index, juce::int64 v);
    SqliteStatement& bind (int index, int v) { return bind (index, (juce::int64) v); }
    SqliteStatement& bind (int index, double v);
    SqliteStatement& bind (int index, const juce::String& v);
    SqliteStatement& bindNull (int index);

    /** Returns true while rows are available. */
    bool step();
    /** Runs a statement that returns no rows. */
    bool execute();
    void reset();

    juce::int64 getInt64 (int column) const;
    int getInt (int column) const { return (int) getInt64 (column); }
    double getDouble (int column) const;
    juce::String getText (int column) const;
    bool isNull (int column) const;

private:
    SqliteDb* owner;
    sqlite3_stmt* stmt = nullptr;
};

class SqliteDb
{
public:
    SqliteDb() = default;
    ~SqliteDb();

    bool open (const juce::File& file, juce::String& error);
    bool openInMemory (juce::String& error);
    void close();
    bool isOpen() const noexcept { return db != nullptr; }

    bool exec (const char* sql);
    juce::String lastError() const;
    juce::int64 lastInsertId() const;
    int changes() const;

    std::recursive_mutex& getMutex() noexcept { return mutex; }
    sqlite3* handle() noexcept { return db; }

    /** RAII transaction (BEGIN IMMEDIATE / COMMIT, ROLLBACK if not committed). */
    class Transaction
    {
    public:
        explicit Transaction (SqliteDb& d) : db (d), lock (d.mutex) { db.exec ("BEGIN IMMEDIATE"); }
        ~Transaction() { if (! done) db.exec ("ROLLBACK"); }
        void commit() { db.exec ("COMMIT"); done = true; }
    private:
        SqliteDb& db;
        std::lock_guard<std::recursive_mutex> lock;
        bool done = false;
    };

private:
    sqlite3* db = nullptr;
    std::recursive_mutex mutex;
};

} // namespace nx
