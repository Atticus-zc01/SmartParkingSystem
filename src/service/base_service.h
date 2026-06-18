#pragma once
#include <string>
#include <memory>
#include <vector>
#include "../database/mysql_pool.h"

class BaseService {
public:
    virtual ~BaseService() = default;

    BaseService(const BaseService&) = delete;
    BaseService& operator=(const BaseService&) = delete;

protected:
    BaseService() = default;

    std::shared_ptr<MySQLPool::ConnGuard> getConnection() {
        return MySQLPool::instance().getConnection();
    }

    static std::string escape(MYSQL* mysql, const std::string& s);
    static std::string quote(MYSQL* mysql, const std::string& s);
    bool executeQuery(MYSQL* mysql, const std::string& sql) { return mysql_query(mysql, sql.c_str()) == 0; }
    int executeQueryAffected(MYSQL* mysql, const std::string& sql);
};
