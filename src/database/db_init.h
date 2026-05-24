#pragma once
#include <string>
#include "mysql_pool.h"
#include "../config.h"
#include "../sha256.h"

class DBInit {
public:
    static bool createDatabase(const AppConfig& cfg);
    static bool createTables(const AppConfig& cfg);
};
