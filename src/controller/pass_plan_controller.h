#pragma once
#include "base_controller.h"

class PassPlanController : public BaseController {
public:
    std::string getPrefix() const override;
    void registerRoutes(crow::SimpleApp& app) override;
};
