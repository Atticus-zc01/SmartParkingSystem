#include "parking_controller.h"
#include "../service/parking_service.h"
#include "../service/billing_service.h"
#include "../config.h"
#include "../permissions.h"

std::string ParkingController::getPrefix() const { return "/api/parking"; }

void ParkingController::registerRoutes(crow::SimpleApp& app) {
    CROW_ROUTE(app, "/api/parking/list").methods("GET"_method)([](const crow::request& req) {
        if (!BaseController::isAuthenticated(req))
            return BaseController::errorResponse(401, "请先登录");
        auto conn = MySQLPool::instance().getConnection();
        crow::json::wvalue res;
        std::vector<crow::json::wvalue> arr;
        if (conn) {
            if (mysql_query(conn->get(), "SELECT P_id,P_name,P_total_count,P_current_count,P_reserve_count,P_fee FROM PARKING_LOT") == 0) {
                MYSQL_RES* r = mysql_store_result(conn->get());
                if (r) {
                    MYSQL_ROW row;
                    while ((row = mysql_fetch_row(r))) {
                        crow::json::wvalue lot;
                        lot["P_id"] = std::stoi(row[0]);
                        lot["P_name"] = row[1] ? row[1] : "";
                        lot["P_total_count"] = std::stoi(row[2]);
                        lot["P_current_count"] = std::stoi(row[3]);
                        lot["P_reserve_count"] = std::stoi(row[4]);
                        lot["P_fee"] = row[5] ? std::stod(row[5]) : 5.0;
                        arr.push_back(std::move(lot));
                    }
                    mysql_free_result(r);
                }
            }
        }
        res["lots"] = std::move(arr);
        return crow::response(res);
    });

    CROW_ROUTE(app, "/api/parking/status").methods("GET"_method)([](const crow::request& req) {
        if (!BaseController::checkPermission(req, Permissions::PARKING_VIEW))
            return BaseController::errorResponse(403, "权限不足");
        auto lot = ParkingService::instance().getStatus();
        crow::response r(lot.serialize());
        r.set_header("Content-Type", "application/json; charset=utf-8");
        return r;
    });

    CROW_ROUTE(app, "/api/parking/stats").methods("GET"_method)([](const crow::request& req) {
        if (!BaseController::checkPermission(req, Permissions::PARKING_VIEW))
            return BaseController::errorResponse(403, "权限不足");
        auto lot = ParkingService::instance().getStatus();
        crow::json::wvalue res;
        res["reserved"] = lot.P_reserve_count;
        res["occupied"] = lot.P_current_count;
        res["available"] = lot.P_total_count - lot.P_current_count - lot.P_reserve_count;
        res["total"] = lot.P_total_count;
        return crow::response(res);
    });

    CROW_ROUTE(app, "/api/parking/settings").methods("PUT"_method)([](const crow::request& req) {
        if (!BaseController::checkPermission(req, Permissions::PARKING_SETTINGS))
            return BaseController::errorResponse(403, "权限不足");
        auto body = BaseController::parseBody(req);
        if (!body) return BaseController::errorResponse(400, "Invalid JSON");

        double fee = body["fee"].d();
        int total = body["capacity"].i();
        if (!ParkingService::instance().updateSettings(fee, total))
            return BaseController::errorResponse(400, "更新失败");

        AppConfig::instance().fee = fee;
        AppConfig::instance().capacity = total;
        return BaseController::successResponse("更新成功");
    });

    CROW_ROUTE(app, "/api/parking/billing-rules").methods("GET"_method)([](const crow::request& req) {
        if (!BaseController::checkPermission(req, Permissions::BILLING_VIEW))
            return BaseController::errorResponse(403, "权限不足");
        auto rules = BillingService::instance().getRules();
        crow::json::wvalue res;
        res["rules"] = BaseController::toJsonArray(rules);
        return crow::response(res);
    });

    CROW_ROUTE(app, "/api/parking/billing-rules/<int>").methods("PUT"_method)([](const crow::request& req, int id) {
        if (!BaseController::checkPermission(req, Permissions::BILLING_MANAGE))
            return BaseController::errorResponse(403, "权限不足");
        auto body = BaseController::parseBody(req);
        if (!body) return BaseController::errorResponse(400, "Invalid JSON");

        BillingRule rule;
        rule.rule_name = body["rule_name"].s();
        rule.rule_type = body["rule_type"].s();
        rule.free_minutes = body["free_minutes"].i();
        rule.hourly_rate = body["hourly_rate"].d();
        rule.max_daily_fee = body["max_daily_fee"].d();
        rule.tier_config = body.has("tier_config") ? std::string(body["tier_config"].s()) : std::string("");
        rule.description = body.has("description") ? std::string(body["description"].s()) : std::string("");
        rule.is_active = body["is_active"].b();
        if (!BillingService::instance().updateRule(id, rule))
            return BaseController::errorResponse(400, "更新失败");
        return BaseController::successResponse("更新成功");
    });

    CROW_ROUTE(app, "/api/parking/monthly-passes").methods("GET"_method)([](const crow::request& req) {
        if (!BaseController::checkPermission(req, Permissions::BILLING_MANAGE))
            return BaseController::errorResponse(403, "权限不足");
        auto passes = BillingService::instance().getMonthlyPasses();
        crow::json::wvalue res;
        res["passes"] = BaseController::toJsonArray(passes);
        return crow::response(res);
    });

    CROW_ROUTE(app, "/api/parking/monthly-passes").methods("POST"_method)([](const crow::request& req) {
        if (!BaseController::isAuthenticated(req))
            return BaseController::errorResponse(401, "请先登录");
        auto body = BaseController::parseBody(req);
        if (!body) return BaseController::errorResponse(400, "Invalid JSON");
        MonthlyPass pass;
        pass.license_plate = body["license_plate"].s();
        pass.pass_type = body["pass_type"].s();
        pass.start_date = body["start_date"].s();
        pass.end_date = body["end_date"].s();
        pass.fee = body["fee"].d();
        if (!BillingService::instance().addMonthlyPass(pass))
            return BaseController::errorResponse(400, "添加失败");
        return BaseController::successResponse("添加成功");
    });

    CROW_ROUTE(app, "/api/parking/monthly-passes/<int>").methods("PUT"_method)([](const crow::request& req, int id) {
        if (!BaseController::checkPermission(req, Permissions::BILLING_MANAGE))
            return BaseController::errorResponse(403, "权限不足");
        auto body = BaseController::parseBody(req);
        if (!body) return BaseController::errorResponse(400, "Invalid JSON");
        MonthlyPass pass;
        pass.license_plate = body["license_plate"].s();
        pass.pass_type = body["pass_type"].s();
        pass.start_date = body["start_date"].s();
        pass.end_date = body["end_date"].s();
        pass.fee = body["fee"].d();
        if (!BillingService::instance().updateMonthlyPass(id, pass))
            return BaseController::errorResponse(400, "更新失败");
        return BaseController::successResponse("更新成功");
    });

    CROW_ROUTE(app, "/api/parking/monthly-passes/<int>").methods("DELETE"_method)([](const crow::request& req, int id) {
        if (!BaseController::checkPermission(req, Permissions::BILLING_MANAGE))
            return BaseController::errorResponse(403, "权限不足");
        if (!BillingService::instance().deleteMonthlyPass(id))
            return BaseController::errorResponse(400, "删除失败");
        return BaseController::successResponse("删除成功");
    });
}
