#pragma once
#include "base_service.h"
#include "../model/parking_lot.h"

class ParkingService : public BaseService {
public:
    static ParkingService& instance();
    ParkingLot getStatus(const std::string& P_name = "");
    std::vector<ParkingLot> getAllLots();
    bool updateSettings(double fee, int total_count);
private:
    ParkingService() = default;
};
