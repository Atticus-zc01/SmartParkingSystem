#pragma once
#include <string>
#include "base_model.h"

class BalanceRecord : public BaseModel {
public:
    int id = 0;
    int user_id = 0;
    double amount = 0.0;
    std::string type;      // recharge/purchase/checkout/refund
    std::string description;
    double balance_after = 0.0;
    std::string created_at;

    int getId() const override { return id; }
    void setId(int id_) override { id = id_; }
    std::string getTableName() const override { return "BALANCE_RECORD"; }

    crow::json::wvalue serialize() const override {
        crow::json::wvalue j;
        j["id"] = id;
        j["user_id"] = user_id;
        j["amount"] = amount;
        j["type"] = type;
        j["description"] = description;
        j["balance_after"] = balance_after;
        j["created_at"] = created_at;
        return j;
    }
};
