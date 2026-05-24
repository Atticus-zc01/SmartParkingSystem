#include "base_service.h"

std::string BaseService::escape(MYSQL* mysql, const std::string& s) {
    std::vector<char> buf(s.size() * 2 + 1);
    size_t len = mysql_real_escape_string(mysql, buf.data(), s.c_str(), (unsigned long)s.size());
    return std::string(buf.data(), len);
}

std::string BaseService::quote(MYSQL* mysql, const std::string& s) {
    return "'" + escape(mysql, s) + "'";
}

int BaseService::executeQueryAffected(MYSQL* mysql, const std::string& sql) {
    if (mysql_query(mysql, sql.c_str()) != 0) return -1;
    return (int)mysql_affected_rows(mysql);
}
