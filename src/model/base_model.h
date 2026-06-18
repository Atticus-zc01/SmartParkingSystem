#pragma once
#include <string>
#include "crow.h"

class BaseModel {
public:
    virtual ~BaseModel() = default;

    virtual int getId() const = 0;
    virtual void setId(int id) = 0;
    virtual std::string getTableName() const = 0;
    virtual crow::json::wvalue serialize() const = 0;
};
