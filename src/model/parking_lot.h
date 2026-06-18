#pragma once
#include <string>
#include "base_model.h"
#include "../config.h"

class ParkingLot : public BaseModel {
public:
    int P_id = 0;
    std::string P_name;
    int P_total_count = 0;
    int P_current_count = 0;
    int P_reserve_count = 0;
    double P_fee = 5.00;

    int getId() const override { return P_id; }
    void setId(int id_) override { P_id = id_; }
    std::string getTableName() const override { return "PARKING_LOT"; }

    crow::json::wvalue serialize() const override {
        crow::json::wvalue j;
        j["P_id"] = P_id;
        j["P_name"] = P_name;
        j["P_total_count"] = P_total_count;
        j["P_current_count"] = P_current_count;
        j["P_reserve_count"] = P_reserve_count;
        j["P_available_count"] = P_total_count - P_current_count - P_reserve_count;
        j["P_fee"] = P_fee;
        j["notice_expire_minutes"] = AppConfig::instance().notice_expire_minutes;
        return j;
    }
};
