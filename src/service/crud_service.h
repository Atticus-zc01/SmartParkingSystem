#pragma once
#include <vector>
#include <string>
#include <mysql.h>
#include "base_service.h"
#include "../model/base_model.h"

template <typename T>
class CrudService : public BaseService {
public:
    virtual ~CrudService() = default;

protected:
    CrudService() = default;

    std::vector<T> list(const std::string& sql) {
        std::vector<T> results;
        auto conn = getConnection();
        if (!conn) return results;

        if (mysql_query(conn->get(), sql.c_str()) != 0)
            return results;
        MYSQL_RES* res = mysql_store_result(conn->get());
        if (!res) return results;

        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
            results.push_back(mapRow(row));
        }
        mysql_free_result(res);
        return results;
    }

    bool deleteById(int id) {
        auto conn = getConnection();
        if (!conn) return false;
        T temp;
        std::string sql = "DELETE FROM " + temp.getTableName() + " WHERE id=" + std::to_string(id);
        return executeQuery(conn->get(), sql);
    }

    bool executeUpdate(const std::string& sql) {
        auto conn = getConnection();
        if (!conn) return false;
        return executeQuery(conn->get(), sql);
    }

    virtual T mapRow(MYSQL_ROW row) = 0;
};
