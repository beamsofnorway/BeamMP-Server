// BeamMP, the BeamNG.drive multiplayer mod.
// Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
//
// BeamMP Ltd. can be contacted by electronic mail via contact@beammp.com.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as published
// by the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include "Http.h"

#include "Common.h"
#include "CustomAssert.h"
#include "Env.h"
#include "TControlService.h"

#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <random>
#include <stdexcept>

using json = nlohmann::json;

static size_t CurlWriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    std::string* Result = reinterpret_cast<std::string*>(userp);
    std::string NewContents(reinterpret_cast<char*>(contents), size * nmemb);
    *Result += NewContents;
    return size * nmemb;
}

std::string Http::GET(const std::string& url, unsigned int* status) {
    std::string Ret;
    static thread_local CURL* curl = curl_easy_init();
    if (curl) {
        CURLcode res;
        char errbuf[CURL_ERROR_SIZE];
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&Ret);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10); // seconds
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
        errbuf[0] = 0;
        res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            beammp_error("GET to " + url + " failed: " + std::string(curl_easy_strerror(res)));
            beammp_error("Curl error: " + std::string(errbuf));
            return Http::ErrorString;
        }

        if (status) {
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, status);
        }

    } else {
        beammp_error("Curl easy init failed");
        return Http::ErrorString;
    }
    return Ret;
}

std::string Http::POST(const std::string& url, const std::string& body, const std::string& ContentType, unsigned int* status, const std::map<std::string, std::string>& headers) {
    std::string Ret;
    static thread_local CURL* curl = curl_easy_init();
    if (curl) {
        CURLcode res;
        char errbuf[CURL_ERROR_SIZE];
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&Ret);
        curl_easy_setopt(curl, CURLOPT_POST, 1);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, body.size());
        struct curl_slist* list = nullptr;
        list = curl_slist_append(list, ("Content-Type: " + ContentType).c_str());

        for (auto [header, value] : headers) {
            list = curl_slist_append(list, (header + ": " + value).c_str());
        }

        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10); // seconds
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
        errbuf[0] = 0;
        res = curl_easy_perform(curl);
        curl_slist_free_all(list);
        if (res != CURLE_OK) {
            beammp_error("POST to " + url + " failed: " + std::string(curl_easy_strerror(res)));
            beammp_error("Curl error: " + std::string(errbuf));
            return Http::ErrorString;
        }

        if (status) {
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, status);
        }

    } else {
        beammp_error("Curl easy init failed");
        return Http::ErrorString;
    }
    return Ret;
}

// RFC 2616, RFC 7231
static std::map<size_t, const char*> Map = {
    { -1, "Invalid Response Code" },
    { 100, "Continue" },
    { 101, "Switching Protocols" },
    { 102, "Processing" },
    { 103, "Early Hints" },
    { 200, "OK" },
    { 201, "Created" },
    { 202, "Accepted" },
    { 203, "Non-Authoritative Information" },
    { 204, "No Content" },
    { 205, "Reset Content" },
    { 206, "Partial Content" },
    { 207, "Multi-Status" },
    { 208, "Already Reported" },
    { 226, "IM Used" },
    { 300, "Multiple Choices" },
    { 301, "Moved Permanently" },
    { 302, "Found" },
    { 303, "See Other" },
    { 304, "Not Modified" },
    { 305, "Use Proxy" },
    { 306, "(Unused)" },
    { 307, "Temporary Redirect" },
    { 308, "Permanent Redirect" },
    { 400, "Bad Request" },
    { 401, "Unauthorized" },
    { 402, "Payment Required" },
    { 403, "Forbidden" },
    { 404, "Not Found" },
    { 405, "Method Not Allowed" },
    { 406, "Not Acceptable" },
    { 407, "Proxy Authentication Required" },
    { 408, "Request Timeout" },
    { 409, "Conflict" },
    { 410, "Gone" },
    { 411, "Length Required" },
    { 412, "Precondition Failed" },
    { 413, "Payload Too Large" },
    { 414, "URI Too Long" },
    { 415, "Unsupported Media Type" },
    { 416, "Range Not Satisfiable" },
    { 417, "Expectation Failed" },
    { 421, "Misdirected Request" },
    { 422, "Unprocessable Entity" },
    { 423, "Locked" },
    { 424, "Failed Dependency" },
    { 425, "Too Early" },
    { 426, "Upgrade Required" },
    { 428, "Precondition Required" },
    { 429, "Too Many Requests" },
    { 431, "Request Header Fields Too Large" },
    { 451, "Unavailable For Legal Reasons" },
    { 500, "Internal Server Error" },
    { 501, "Not Implemented" },
    { 502, "Bad Gateway" },
    { 503, "Service Unavailable" },
    { 504, "Gateway Timeout" },
    { 505, "HTTP Version Not Supported" },
    { 506, "Variant Also Negotiates" },
    { 507, "Insufficient Storage" },
    { 508, "Loop Detected" },
    { 510, "Not Extended" },
    { 511, "Network Authentication Required" },
    // cloudflare status codes
    { 520, "(CDN) Web Server Returns An Unknown Error" },
    { 521, "(CDN) Web Server Is Down" },
    { 522, "(CDN) Connection Timed Out" },
    { 523, "(CDN) Origin Is Unreachable" },
    { 524, "(CDN) A Timeout Occurred" },
    { 525, "(CDN) SSL Handshake Failed" },
    { 526, "(CDN) Invalid SSL Certificate" },
    { 527, "(CDN) Railgun Listener To Origin Error" },
    { 530, "(CDN) 1XXX Internal Error" },
};

static const char Magic[] = {
    0x20, 0x2f, 0x5c, 0x5f,
    0x2f, 0x5c, 0x0a, 0x28,
    0x20, 0x6f, 0x2e, 0x6f,
    0x20, 0x29, 0x0a, 0x20,
    0x3e, 0x20, 0x5e, 0x20,
    0x3c, 0x0a, 0x00
};

namespace {
using HttpHandler = std::function<void(const httplib::Request&, httplib::Response&)>;

bool EnvBool(Env::Key Key, bool DefaultValue) {
    auto Value = Env::Get(Key);
    if (!Value.has_value()) {
        return DefaultValue;
    }

    auto Lowered = LowerString(*Value);
    if (Lowered == "0" || Lowered == "false" || Lowered == "no" || Lowered == "off") {
        return false;
    }
    if (Lowered == "1" || Lowered == "true" || Lowered == "yes" || Lowered == "on") {
        return true;
    }
    return DefaultValue;
}

uint16_t EnvPort(Env::Key Key, uint16_t DefaultValue) {
    auto Value = Env::Get(Key);
    if (!Value.has_value()) {
        return DefaultValue;
    }

    try {
        const auto Parsed = std::stoul(*Value);
        if (Parsed == 0 || Parsed > UINT16_MAX) {
            throw std::out_of_range("port out of range");
        }
        return static_cast<uint16_t>(Parsed);
    } catch (const std::exception&) {
        beammp_warn("Invalid HTTP API port in BEAMMP_HTTP_API_PORT, falling back to default");
        return DefaultValue;
    }
}

uint16_t DefaultHttpApiPort() {
    const auto ServerPort = Application::Settings.getAsInt(Settings::Key::General_Port);
    return static_cast<uint16_t>(ServerPort >= int(UINT16_MAX) ? UINT16_MAX - 1 : ServerPort + 1);
}

bool IsLoopbackAddress(const std::string& Address) {
    return Address == "127.0.0.1" || Address == "::1" || Address == "::ffff:127.0.0.1" || Address == "localhost";
}

bool GetBoolParam(const httplib::Request& Req, const std::string& Name, bool DefaultValue) {
    if (!Req.has_param(Name)) {
        return DefaultValue;
    }
    auto Value = LowerString(Req.get_param_value(Name));
    if (Value == "1" || Value == "true" || Value == "yes" || Value == "on") {
        return true;
    }
    if (Value == "0" || Value == "false" || Value == "no" || Value == "off") {
        return false;
    }
    return DefaultValue;
}

std::optional<int> GetIntParam(const httplib::Request& Req, const std::string& Name) {
    if (!Req.has_param(Name)) {
        return std::nullopt;
    }
    try {
        return std::stoi(Req.get_param_value(Name));
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

json ParseJsonBody(const httplib::Request& Req) {
    if (Req.body.empty()) {
        return json::object();
    }
    return json::parse(Req.body);
}

void SetJson(httplib::Response& Res, const json& Body, int Status = 200) {
    Res.status = Status;
    Res.set_content(Body.dump(), "application/json");
}

void SetApiError(httplib::Response& Res, int Status, const std::string& Message) {
    SetJson(Res, {
        { "ok", false },
        { "error", Message },
    }, Status);
}

json ExecuteControlAction(const std::string& Action, json Payload = {}) {
    return Application::Control().Execute(Action, std::move(Payload));
}

void HandleControlAction(httplib::Response& Res, const std::string& Action, json Payload = {}) {
    const auto Result = ExecuteControlAction(Action, std::move(Payload));
    SetJson(Res, Result, Result.value("ok", false) ? 200 : 400);
}

bool IsAuthorized(const httplib::Request& Req, const std::string& Token) {
    if (Token.empty()) {
        return IsLoopbackAddress(Req.remote_addr);
    }

    const auto Header = Req.get_header_value("Authorization");
    const std::string Prefix = "Bearer ";
    if (!Header.starts_with(Prefix)) {
        return false;
    }
    return Header.substr(Prefix.size()) == Token;
}

HttpHandler RequireApiAuth(std::string Token, HttpHandler Next) {
    return [Token = std::move(Token), Next = std::move(Next)](const httplib::Request& Req, httplib::Response& Res) {
        if (!IsAuthorized(Req, Token)) {
            if (!Token.empty()) {
                Res.set_header("WWW-Authenticate", "Bearer realm=\"BeamMP HTTP API\"");
                SetApiError(Res, 401, "Missing or invalid bearer token");
            } else {
                SetApiError(Res, 403, "HTTP API is limited to loopback clients unless BEAMMP_HTTP_API_TOKEN is set");
            }
            return;
        }
        Next(Req, Res);
    };
}

json DescribeHttpApi(bool TokenConfigured, const std::string& BindAddress, uint16_t Port) {
    return {
        { "ok", true },
        { "data", {
              { "name", "BeamMP HTTP API" },
              { "bind_address", BindAddress },
              { "port", Port },
              { "auth", TokenConfigured ? "bearer_token" : "loopback_only" },
              { "endpoints", json::array({
                    "/health",
                    "/api",
                    "/api/actions",
                    "/api/logs/recent",
                    "/api/events/recent",
                    "/api/server/status",
                    "/api/server/version",
                    "/api/server/subsystems",
                    "/api/lua/states",
                    "/api/players",
                    "/api/players/find",
                    "/api/players/get",
                    "/api/players/disconnect",
                    "/api/players/vehicles",
                    "/api/players/vehicle-positions",
                    "/api/players/vehicle-position",
                    "/api/players/vehicle-position/raw",
                    "/api/players/vehicle-position/parsed",
                    "/api/vehicles",
                    "/api/vehicles/positions",
                    "/api/chat/send",
                    "/api/players/kick",
                    "/api/settings",
                    "/api/settings/get",
                    "/api/settings/set",
                    "/api/mods",
                    "/api/mods/reload",
                    "/api/mods/protection",
                    "/api/notifications/send",
                    "/api/dialogs/confirmation",
                    "/api/events/trigger-client",
                    "/api/spatial/teleport",
                    "/api/spatial/rebase",
                    "/api/spatial/offset",
                    "/api/spatial/teleport-applied",
                    "/api/vehicles/remove",
                }) },
          } },
    };
}
}

std::string Http::Status::ToString(int Code) {
    if (Map.find(Code) != Map.end()) {
        return Map.at(Code);
    } else {
        return std::to_string(Code);
    }
}

TEST_CASE("Http::Status::ToString") {
    CHECK(Http::Status::ToString(200) == "OK");
    CHECK(Http::Status::ToString(696969) == "696969");
    CHECK(Http::Status::ToString(-1) == "Invalid Response Code");
}

Http::Server::THttpServerInstance::THttpServerInstance() {
    const auto DefaultPort = DefaultHttpApiPort();
    mEnabled = Application::Settings.getAsBool(Settings::Key::HttpApi_Enabled);
    mBindAddress = Application::Settings.getAsString(Settings::Key::HttpApi_Host);
    mPort = static_cast<uint16_t>(Application::Settings.getAsInt(Settings::Key::HttpApi_Port));
    mAuthToken = Application::Settings.getAsString(Settings::Key::HttpApi_Token);

    mEnabled = EnvBool(Env::Key::HTTP_API_ENABLED, mEnabled);
    mBindAddress = Env::Get(Env::Key::HTTP_API_HOST).value_or(mBindAddress);
    mPort = EnvPort(Env::Key::HTTP_API_PORT, mPort == 0 ? DefaultPort : mPort);
    mAuthToken = Env::Get(Env::Key::HTTP_API_TOKEN).value_or(mAuthToken);

    if (mBindAddress.empty()) {
        mBindAddress = "127.0.0.1";
    }
    if (mPort == 0) {
        mPort = DefaultPort;
    }

    if (!mEnabled) {
        Application::SetSubsystemStatus("HTTPServer", Application::Status::Shutdown);
        beammp_info("HTTP API disabled via BEAMMP_HTTP_API_ENABLED");
        return;
    }

    Application::SetSubsystemStatus("HTTPServer", Application::Status::Starting);
    mServer = std::make_shared<httplib::Server>();
    mThread = std::thread(&Http::Server::THttpServerInstance::operator(), this);
}

Http::Server::THttpServerInstance::~THttpServerInstance() {
    if (mServer) {
        mServer->stop();
    }
    if (mThread.joinable()) {
        mThread.join();
    }
    if (mEnabled) {
        Application::SetSubsystemStatus("HTTPServer", Application::Status::Shutdown);
    }
}

void Http::Server::THttpServerInstance::operator()() try {
    RegisterThread("HTTPServer");
    auto& HttpLibServerInstance = *mServer;

    HttpLibServerInstance.Get("/", [this](const httplib::Request&, httplib::Response& Res) {
        SetJson(Res, DescribeHttpApi(!mAuthToken.empty(), mBindAddress, mPort));
    });
    HttpLibServerInstance.Get("/health", [](const httplib::Request&, httplib::Response& Res) {
        size_t SystemsBad = 0;
        auto Statuses = Application::GetSubsystemStatuses();
        for (const auto& NameStatusPair : Statuses) {
            switch (NameStatusPair.second) {
            case Application::Status::Starting:
            case Application::Status::ShuttingDown:
            case Application::Status::Shutdown:
            case Application::Status::Good:
                break;
            case Application::Status::Bad:
                SystemsBad++;
                break;
            default:
                beammp_assert_not_reachable();
            }
        }
        SetJson(Res, {
            { "ok", SystemsBad == 0 },
        });
    });
    HttpLibServerInstance.Get({ 0x2f, 0x6b, 0x69, 0x74, 0x74, 0x79 }, [](const httplib::Request&, httplib::Response& Res) {
        Res.set_content(std::string(Magic), "text/plain");
    });

    const auto Authed = [this](HttpHandler Next) {
        return RequireApiAuth(mAuthToken, std::move(Next));
    };

    HttpLibServerInstance.Get("/api", Authed([this](const httplib::Request&, httplib::Response& Res) {
        SetJson(Res, DescribeHttpApi(!mAuthToken.empty(), mBindAddress, mPort));
    }));
    HttpLibServerInstance.Get("/api/actions", Authed([](const httplib::Request&, httplib::Response& Res) {
        HandleControlAction(Res, "system.describe_actions");
    }));
    HttpLibServerInstance.Get("/api/logs/recent", Authed([](const httplib::Request& Req, httplib::Response& Res) {
        json Payload = json::object();
        if (Req.has_param("limit")) {
            const auto MaybeLimit = GetIntParam(Req, "limit");
            if (!MaybeLimit.has_value() || *MaybeLimit <= 0) {
                SetApiError(Res, 400, "Query parameter 'limit' must be a positive integer");
                return;
            }
            Payload["limit"] = *MaybeLimit;
        }
        if (Req.has_param("after_sequence")) {
            const auto MaybeAfter = GetIntParam(Req, "after_sequence");
            if (!MaybeAfter.has_value() || *MaybeAfter < 0) {
                SetApiError(Res, 400, "Query parameter 'after_sequence' must be a non-negative integer");
                return;
            }
            Payload["after_sequence"] = *MaybeAfter;
        }
        HandleControlAction(Res, "system.logs.recent", std::move(Payload));
    }));
    HttpLibServerInstance.Get("/api/events/recent", Authed([](const httplib::Request& Req, httplib::Response& Res) {
        json Payload = json::object();
        if (Req.has_param("limit")) {
            const auto MaybeLimit = GetIntParam(Req, "limit");
            if (!MaybeLimit.has_value() || *MaybeLimit <= 0) {
                SetApiError(Res, 400, "Query parameter 'limit' must be a positive integer");
                return;
            }
            Payload["limit"] = *MaybeLimit;
        }
        if (Req.has_param("after_sequence")) {
            const auto MaybeAfter = GetIntParam(Req, "after_sequence");
            if (!MaybeAfter.has_value() || *MaybeAfter < 0) {
                SetApiError(Res, 400, "Query parameter 'after_sequence' must be a non-negative integer");
                return;
            }
            Payload["after_sequence"] = *MaybeAfter;
        }
        HandleControlAction(Res, "system.events.recent", std::move(Payload));
    }));
    HttpLibServerInstance.Get("/api/server/status", Authed([](const httplib::Request&, httplib::Response& Res) {
        HandleControlAction(Res, "server.status");
    }));
    HttpLibServerInstance.Get("/api/server/version", Authed([](const httplib::Request&, httplib::Response& Res) {
        HandleControlAction(Res, "server.version");
    }));
    HttpLibServerInstance.Get("/api/server/subsystems", Authed([](const httplib::Request&, httplib::Response& Res) {
        HandleControlAction(Res, "server.subsystems");
    }));
    HttpLibServerInstance.Get("/api/lua/states", Authed([](const httplib::Request&, httplib::Response& Res) {
        HandleControlAction(Res, "lua.states.list");
    }));
    HttpLibServerInstance.Get("/api/players", Authed([](const httplib::Request& Req, httplib::Response& Res) {
        HandleControlAction(Res, "players.list", {
            { "include_vehicles", GetBoolParam(Req, "include_vehicles", false) },
        });
    }));
    HttpLibServerInstance.Get("/api/players/get", Authed([](const httplib::Request& Req, httplib::Response& Res) {
        json Payload = json::object();
        if (Req.has_param("id")) {
            const auto MaybeID = GetIntParam(Req, "id");
            if (!MaybeID.has_value()) {
                SetApiError(Res, 400, "Query parameter 'id' must be an integer");
                return;
            }
            Payload["id"] = *MaybeID;
        } else if (Req.has_param("name")) {
            Payload["name"] = Req.get_param_value("name");
            Payload["prefix_match"] = GetBoolParam(Req, "prefix_match", true);
        } else {
            SetApiError(Res, 400, "Expected query parameter 'id' or 'name'");
            return;
        }
        HandleControlAction(Res, "players.get", std::move(Payload));
    }));
    HttpLibServerInstance.Get("/api/players/find", Authed([](const httplib::Request& Req, httplib::Response& Res) {
        if (!Req.has_param("name")) {
            SetApiError(Res, 400, "Expected query parameter 'name'");
            return;
        }
        json Payload = {
            { "name", Req.get_param_value("name") },
            { "prefix_match", GetBoolParam(Req, "prefix_match", true) },
        };
        if (Req.has_param("limit")) {
            const auto MaybeLimit = GetIntParam(Req, "limit");
            if (!MaybeLimit.has_value() || *MaybeLimit <= 0) {
                SetApiError(Res, 400, "Query parameter 'limit' must be a positive integer");
                return;
            }
            Payload["limit"] = *MaybeLimit;
        }
        HandleControlAction(Res, "players.find", std::move(Payload));
    }));
    HttpLibServerInstance.Get("/api/players/vehicles", Authed([](const httplib::Request& Req, httplib::Response& Res) {
        json Payload = json::object();
        if (Req.has_param("id")) {
            const auto MaybeID = GetIntParam(Req, "id");
            if (!MaybeID.has_value()) {
                SetApiError(Res, 400, "Query parameter 'id' must be an integer");
                return;
            }
            Payload["id"] = *MaybeID;
        } else if (Req.has_param("name")) {
            Payload["name"] = Req.get_param_value("name");
            Payload["prefix_match"] = GetBoolParam(Req, "prefix_match", true);
        } else {
            SetApiError(Res, 400, "Expected query parameter 'id' or 'name'");
            return;
        }
        Payload["include_data"] = GetBoolParam(Req, "include_data", true);
        Payload["include_position_raw"] = GetBoolParam(Req, "include_position_raw", true);
        Payload["include_position_parsed"] = GetBoolParam(Req, "include_position_parsed", false);
        HandleControlAction(Res, "players.vehicles.list", std::move(Payload));
    }));
    HttpLibServerInstance.Get("/api/players/vehicle-positions", Authed([](const httplib::Request& Req, httplib::Response& Res) {
        json Payload = {
            { "include_parsed", GetBoolParam(Req, "include_parsed", true) },
        };
        if (Req.has_param("id")) {
            const auto MaybeID = GetIntParam(Req, "id");
            if (!MaybeID.has_value()) {
                SetApiError(Res, 400, "Query parameter 'id' must be an integer");
                return;
            }
            Payload["id"] = *MaybeID;
        } else if (Req.has_param("name")) {
            Payload["name"] = Req.get_param_value("name");
            Payload["prefix_match"] = GetBoolParam(Req, "prefix_match", true);
        }
        HandleControlAction(Res, "players.vehicle_positions", std::move(Payload));
    }));
    HttpLibServerInstance.Get("/api/players/vehicle-position", Authed([](const httplib::Request& Req, httplib::Response& Res) {
        if (!Req.has_param("vehicle_id")) {
            SetApiError(Res, 400, "Expected query parameter 'vehicle_id'");
            return;
        }
        const auto MaybeVehicleID = GetIntParam(Req, "vehicle_id");
        if (!MaybeVehicleID.has_value()) {
            SetApiError(Res, 400, "Query parameter 'vehicle_id' must be an integer");
            return;
        }
        json Payload = {
            { "vehicle_id", *MaybeVehicleID },
            { "include_parsed", GetBoolParam(Req, "include_parsed", true) },
        };
        if (Req.has_param("player_id")) {
            const auto MaybePlayerID = GetIntParam(Req, "player_id");
            if (!MaybePlayerID.has_value()) {
                SetApiError(Res, 400, "Query parameter 'player_id' must be an integer");
                return;
            }
            Payload["player_id"] = *MaybePlayerID;
        } else if (Req.has_param("player_name")) {
            Payload["player_name"] = Req.get_param_value("player_name");
            Payload["prefix_match"] = GetBoolParam(Req, "prefix_match", true);
        } else {
            SetApiError(Res, 400, "Expected query parameter 'player_id' or 'player_name'");
            return;
        }
        HandleControlAction(Res, "players.vehicle_position", std::move(Payload));
    }));
    HttpLibServerInstance.Get("/api/players/vehicle-position/raw", Authed([](const httplib::Request& Req, httplib::Response& Res) {
        if (!Req.has_param("vehicle_id")) {
            SetApiError(Res, 400, "Expected query parameter 'vehicle_id'");
            return;
        }
        const auto MaybeVehicleID = GetIntParam(Req, "vehicle_id");
        if (!MaybeVehicleID.has_value()) {
            SetApiError(Res, 400, "Query parameter 'vehicle_id' must be an integer");
            return;
        }
        json Payload = {
            { "vehicle_id", *MaybeVehicleID },
            { "include_parsed", false },
        };
        if (Req.has_param("player_id")) {
            const auto MaybePlayerID = GetIntParam(Req, "player_id");
            if (!MaybePlayerID.has_value()) {
                SetApiError(Res, 400, "Query parameter 'player_id' must be an integer");
                return;
            }
            Payload["player_id"] = *MaybePlayerID;
        } else if (Req.has_param("player_name")) {
            Payload["player_name"] = Req.get_param_value("player_name");
            Payload["prefix_match"] = GetBoolParam(Req, "prefix_match", true);
        } else {
            SetApiError(Res, 400, "Expected query parameter 'player_id' or 'player_name'");
            return;
        }
        const auto Result = ExecuteControlAction("players.vehicle_position", std::move(Payload));
        if (!Result.value("ok", false)) {
            SetJson(Res, Result, 400);
            return;
        }
        const auto& Vehicle = Result.at("data").at("vehicle");
        SetJson(Res, {
            { "ok", true },
            { "player", Result.at("data").at("player") },
            { "vehicle_id", Vehicle.at("vehicle_id") },
            { "position_raw", Vehicle.at("position_raw") },
        });
    }));
    HttpLibServerInstance.Get("/api/players/vehicle-position/parsed", Authed([](const httplib::Request& Req, httplib::Response& Res) {
        if (!Req.has_param("vehicle_id")) {
            SetApiError(Res, 400, "Expected query parameter 'vehicle_id'");
            return;
        }
        const auto MaybeVehicleID = GetIntParam(Req, "vehicle_id");
        if (!MaybeVehicleID.has_value()) {
            SetApiError(Res, 400, "Query parameter 'vehicle_id' must be an integer");
            return;
        }
        json Payload = {
            { "vehicle_id", *MaybeVehicleID },
            { "include_parsed", true },
        };
        if (Req.has_param("player_id")) {
            const auto MaybePlayerID = GetIntParam(Req, "player_id");
            if (!MaybePlayerID.has_value()) {
                SetApiError(Res, 400, "Query parameter 'player_id' must be an integer");
                return;
            }
            Payload["player_id"] = *MaybePlayerID;
        } else if (Req.has_param("player_name")) {
            Payload["player_name"] = Req.get_param_value("player_name");
            Payload["prefix_match"] = GetBoolParam(Req, "prefix_match", true);
        } else {
            SetApiError(Res, 400, "Expected query parameter 'player_id' or 'player_name'");
            return;
        }
        const auto Result = ExecuteControlAction("players.vehicle_position", std::move(Payload));
        if (!Result.value("ok", false)) {
            SetJson(Res, Result, 400);
            return;
        }
        const auto& Vehicle = Result.at("data").at("vehicle");
        SetJson(Res, {
            { "ok", true },
            { "player", Result.at("data").at("player") },
            { "vehicle_id", Vehicle.at("vehicle_id") },
            { "position", Vehicle.contains("position") ? Vehicle.at("position") : json(nullptr) },
        });
    }));
    HttpLibServerInstance.Get("/api/vehicles", Authed([](const httplib::Request& Req, httplib::Response& Res) {
        HandleControlAction(Res, "vehicles.list_all", {
            { "include_data", GetBoolParam(Req, "include_data", true) },
            { "include_position_raw", GetBoolParam(Req, "include_position_raw", true) },
            { "include_position_parsed", GetBoolParam(Req, "include_position_parsed", false) },
        });
    }));
    HttpLibServerInstance.Get("/api/vehicles/positions", Authed([](const httplib::Request& Req, httplib::Response& Res) {
        HandleControlAction(Res, "vehicles.position_snapshots", {
            { "include_parsed", GetBoolParam(Req, "include_parsed", true) },
        });
    }));
    HttpLibServerInstance.Get("/api/settings", Authed([](const httplib::Request&, httplib::Response& Res) {
        HandleControlAction(Res, "settings.list");
    }));
    HttpLibServerInstance.Get("/api/settings/get", Authed([](const httplib::Request& Req, httplib::Response& Res) {
        if (!Req.has_param("category") || !Req.has_param("key")) {
            SetApiError(Res, 400, "Expected query parameters 'category' and 'key'");
            return;
        }
        HandleControlAction(Res, "settings.get", {
            { "category", Req.get_param_value("category") },
            { "key", Req.get_param_value("key") },
        });
    }));
    HttpLibServerInstance.Get("/api/mods", Authed([](const httplib::Request&, httplib::Response& Res) {
        HandleControlAction(Res, "resources.mods.list");
    }));
    HttpLibServerInstance.Get("/api/spatial/offset", Authed([](const httplib::Request& Req, httplib::Response& Res) {
        json Payload;
        if (Req.has_param("player_id")) {
            const auto MaybePlayerID = GetIntParam(Req, "player_id");
            if (!MaybePlayerID.has_value()) {
                SetApiError(Res, 400, "Query parameter 'player_id' must be an integer");
                return;
            }
            Payload["player_id"] = *MaybePlayerID;
        } else if (Req.has_param("id")) {
            const auto MaybeID = GetIntParam(Req, "id");
            if (!MaybeID.has_value()) {
                SetApiError(Res, 400, "Query parameter 'id' must be an integer");
                return;
            }
            Payload["id"] = *MaybeID;
        } else if (Req.has_param("player_name")) {
            Payload["player_name"] = Req.get_param_value("player_name");
            Payload["prefix_match"] = GetBoolParam(Req, "prefix_match", true);
        } else if (Req.has_param("name")) {
            Payload["name"] = Req.get_param_value("name");
            Payload["prefix_match"] = GetBoolParam(Req, "prefix_match", true);
        } else {
            SetApiError(Res, 400, "Expected query parameter 'player_id', 'id', 'player_name', or 'name'");
            return;
        }
        HandleControlAction(Res, "spatial.offset.get", std::move(Payload));
    }));

    HttpLibServerInstance.Post("/api/chat/send", Authed([](const httplib::Request& Req, httplib::Response& Res) {
        HandleControlAction(Res, "chat.send", ParseJsonBody(Req));
    }));
    HttpLibServerInstance.Post("/api/players/kick", Authed([](const httplib::Request& Req, httplib::Response& Res) {
        HandleControlAction(Res, "players.kick", ParseJsonBody(Req));
    }));
    HttpLibServerInstance.Post("/api/players/disconnect", Authed([](const httplib::Request& Req, httplib::Response& Res) {
        HandleControlAction(Res, "players.disconnect", ParseJsonBody(Req));
    }));
    HttpLibServerInstance.Post("/api/settings/set", Authed([](const httplib::Request& Req, httplib::Response& Res) {
        HandleControlAction(Res, "settings.set", ParseJsonBody(Req));
    }));
    HttpLibServerInstance.Post("/api/mods/reload", Authed([](const httplib::Request&, httplib::Response& Res) {
        HandleControlAction(Res, "resources.mods.reload");
    }));
    HttpLibServerInstance.Post("/api/mods/protection", Authed([](const httplib::Request& Req, httplib::Response& Res) {
        HandleControlAction(Res, "resources.mods.set_protected", ParseJsonBody(Req));
    }));
    HttpLibServerInstance.Post("/api/notifications/send", Authed([](const httplib::Request& Req, httplib::Response& Res) {
        HandleControlAction(Res, "notifications.send", ParseJsonBody(Req));
    }));
    HttpLibServerInstance.Post("/api/dialogs/confirmation", Authed([](const httplib::Request& Req, httplib::Response& Res) {
        HandleControlAction(Res, "dialogs.confirmation", ParseJsonBody(Req));
    }));
    HttpLibServerInstance.Post("/api/events/trigger-client", Authed([](const httplib::Request& Req, httplib::Response& Res) {
        HandleControlAction(Res, "events.trigger_client", ParseJsonBody(Req));
    }));
    HttpLibServerInstance.Post("/api/spatial/teleport", Authed([](const httplib::Request& Req, httplib::Response& Res) {
        HandleControlAction(Res, "spatial.teleport", ParseJsonBody(Req));
    }));
    HttpLibServerInstance.Post("/api/spatial/rebase", Authed([](const httplib::Request& Req, httplib::Response& Res) {
        HandleControlAction(Res, "spatial.rebase", ParseJsonBody(Req));
    }));
    HttpLibServerInstance.Post("/api/spatial/offset", Authed([](const httplib::Request& Req, httplib::Response& Res) {
        HandleControlAction(Res, "spatial.offset.set", ParseJsonBody(Req));
    }));
    HttpLibServerInstance.Post("/api/spatial/teleport-applied", Authed([](const httplib::Request& Req, httplib::Response& Res) {
        HandleControlAction(Res, "spatial.teleport_applied", ParseJsonBody(Req));
    }));
    HttpLibServerInstance.Post("/api/vehicles/remove", Authed([](const httplib::Request& Req, httplib::Response& Res) {
        HandleControlAction(Res, "vehicles.remove", ParseJsonBody(Req));
    }));

    HttpLibServerInstance.set_exception_handler([](const httplib::Request&, httplib::Response& Res, std::exception_ptr Ptr) {
        try {
            if (Ptr) {
                std::rethrow_exception(Ptr);
            }
        } catch (const json::exception& e) {
            SetApiError(Res, 400, e.what());
            return;
        } catch (const std::exception& e) {
            SetApiError(Res, 500, e.what());
            return;
        }
        SetApiError(Res, 500, "Unknown HTTP API error");
    });
    HttpLibServerInstance.set_error_handler([](const httplib::Request&, httplib::Response& Res) {
        if (Res.status == 404) {
            SetApiError(Res, 404, "Endpoint not found");
        }
    });
    HttpLibServerInstance.set_logger([](const httplib::Request& Req, const httplib::Response& Res) {
        beammp_debug("Http Server: " + Req.method + " " + Req.target + " -> " + std::to_string(Res.status));
    });

    Application::SetSubsystemStatus("HTTPServer", Application::Status::Good);
    beammp_infof("HTTP API listening on {}:{} ({})", mBindAddress, mPort, mAuthToken.empty() ? "loopback-only" : "bearer token required");
    if (!HttpLibServerInstance.listen(mBindAddress.c_str(), mPort)) {
        Application::SetSubsystemStatus("HTTPServer", Application::Status::Bad);
        throw std::runtime_error("Failed to bind/listen on configured HTTP API address");
    }
} catch (const std::exception& e) {
    Application::SetSubsystemStatus("HTTPServer", Application::Status::Bad);
    beammp_error("Failed to start HTTP API server. Check BEAMMP_HTTP_API_* environment overrides if you changed them. Error: " + std::string(e.what()));
}
