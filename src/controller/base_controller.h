#pragma once
#include "crow.h"
#include <string>
#include <utility>
#include "../permissions.h"

class BaseController {
public:
    virtual ~BaseController() = default;

    virtual void registerRoutes(crow::SimpleApp& app) = 0;
    virtual std::string getPrefix() const = 0;

public:
    BaseController() = default;

    // Helper: convert a vector of serializable models to a JSON array
    template <typename T>
    static std::vector<crow::json::wvalue> toJsonArray(const std::vector<T>& items) {
        std::vector<crow::json::wvalue> arr;
        for (const auto& item : items) arr.push_back(item.serialize());
        return arr;
    }

    // Helper: wrap a JSON array in a response with a key name
    template <typename T>
    static crow::response jsonResponse(const std::string& key, const std::vector<T>& items) {
        crow::json::wvalue res;
        res[key] = toJsonArray(items);
        crow::response r(res);
        r.set_header("Content-Type", "application/json; charset=utf-8");
        return r;
    }

    static crow::json::wvalue jsonValue(double v) { return crow::json::wvalue(v); }
    static crow::json::wvalue jsonValue(int v) { return crow::json::wvalue(v); }
    static crow::json::wvalue jsonValue(const std::string& v) { return crow::json::wvalue(v); }

    // Build a JSON array from a vector of strings
    static std::vector<crow::json::wvalue> toJsonArray(const std::vector<std::string>& items) {
        std::vector<crow::json::wvalue> arr;
        for (const auto& item : items) arr.push_back(crow::json::wvalue(item));
        return arr;
    }

    static crow::response errorResponse(int code, const std::string& message);
    static crow::response successResponse(const std::string& message);

    static bool checkPermission(const crow::request& req, const std::string& permission);
    static bool checkPermissions(const crow::request& req, const std::vector<std::string>& permissions);
    static bool isAdmin(const crow::request& req);
    static bool isRoot(const crow::request& req);

    static crow::json::rvalue parseBody(const crow::request& req);
    static bool isAuthenticated(const crow::request& req);

protected:
    static std::string extractToken(const crow::request& req);
    static std::pair<int, std::string> authenticate(const crow::request& req);
    static std::string getRole(const crow::request& req);
};
