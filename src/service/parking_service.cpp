#include "parking_service.h"
#include "../config.h"

ParkingService& ParkingService::instance() {
    static ParkingService inst;
    return inst;
}

ParkingLot ParkingService::getStatus(const std::string& P_name) {
    ParkingLot lot;
    const auto& cfg = AppConfig::instance();
    std::string name = P_name.empty() ? cfg.parking_name : P_name;
    lot.P_name = name;
    lot.P_total_count = cfg.capacity;
    lot.P_fee = cfg.fee;
    auto conn = getConnection();
    if (!conn) return lot;
    MYSQL* mysql = conn->get();

    std::string sql = "SELECT P_id,P_name,P_total_count,P_current_count,P_reserve_count,P_fee FROM PARKING_LOT WHERE P_name=" +
        quote(mysql, name);
    if (mysql_query(mysql, sql.c_str()) != 0) return lot;
    MYSQL_RES* res = mysql_store_result(mysql);
    if (!res) return lot;

    MYSQL_ROW row = mysql_fetch_row(res);
    if (row) {
        lot.P_id = std::stoi(row[0]);
        lot.P_name = row[1] ? row[1] : "";
        lot.P_total_count = std::stoi(row[2]);
        lot.P_current_count = std::stoi(row[3]);
        lot.P_reserve_count = std::stoi(row[4]);
        lot.P_fee = std::stod(row[5]);
    }
    mysql_free_result(res);
    return lot;
}

std::vector<ParkingLot> ParkingService::getAllLots() {
    std::vector<ParkingLot> lots;
    auto conn = getConnection();
    if (!conn) return lots;
    MYSQL* mysql = conn->get();
    std::string sql = "SELECT P_id,P_name,P_total_count,P_current_count,P_reserve_count,P_fee FROM PARKING_LOT ORDER BY P_id";
    if (mysql_query(mysql, sql.c_str()) != 0) return lots;
    MYSQL_RES* res = mysql_store_result(mysql);
    if (!res) return lots;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        ParkingLot lot;
        lot.P_id = std::stoi(row[0]);
        lot.P_name = row[1] ? row[1] : "";
        lot.P_total_count = std::stoi(row[2]);
        lot.P_current_count = std::stoi(row[3]);
        lot.P_reserve_count = std::stoi(row[4]);
        lot.P_fee = std::stod(row[5]);
        lots.push_back(lot);
    }
    mysql_free_result(res);
    return lots;
}

bool ParkingService::updateSettings(double fee, int total_count) {
    auto conn = getConnection();
    if (!conn) return false;
    MYSQL* mysql = conn->get();

    const auto& cfg = AppConfig::instance();
    std::string sql = "UPDATE PARKING_LOT SET P_fee=" + std::to_string(fee) +
        ", P_total_count=" + std::to_string(total_count) +
        " WHERE P_name=" + quote(mysql, cfg.parking_name);
    if (!executeQuery(mysql, sql)) return false;

    AppConfig::instance().fee = fee;
    AppConfig::instance().capacity = total_count;
    return true;
}
