#pragma once
#include <QString>
#include <memory>
#include <mutex>
#include "duckdb.hpp"

namespace freight::db {

class DuckDBManager {
public:
    static DuckDBManager& Instance();

    bool Init(const QString &db_path);

    bool LoadRulesFromSQLite(const QString &rules_db_path);
    bool ReloadRules(const QString &rules_db_path);

    duckdb::Connection CreateConnection();

    duckdb::DuckDB* GetDB() { return db_.get(); }

    bool ImportFromFile(const QString &table_name, const QString &file_path);
    bool ExportToFile(const QString &table_name, const QString &file_path);

    int64_t GetRowCount(const QString &table_name);

    bool TableExists(const QString &table_name);

    void ResetDB();

private:
    DuckDBManager() = default;
    ~DuckDBManager() = default;

    std::unique_ptr<duckdb::DuckDB> db_;
    std::mutex mutex_;
    QString db_path_;

    bool CreateCalcResultTable(const QString &table_name);
};

} // namespace freight::db
