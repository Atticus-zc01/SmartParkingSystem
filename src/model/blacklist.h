#pragma once
#include <string>
#include "base_model.h"

class BlacklistEntry : public BaseModel {
public:
    int id = 0;
    std::string license_plate;
    std::string reason;
    std::string created_at;

    int getId() const override { return id; }
    void setId(int id_) override { id = id_; }
    std::string getTableName() const override { return "VEHICLE_BLACKLIST"; }

    crow::json::wvalue serialize() const override {
        crow::json::wvalue j;
        j["id"] = id;
        j["license_plate"] = license_plate;
        j["reason"] = reason;
        j["created_at"] = created_at;
        return j;
    }
};
