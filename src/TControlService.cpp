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

#include "TControlService.h"

#include "Client.h"
#include "Common.h"
#include "CustomAssert.h"
#include "LuaAPI.h"
#include "TNetwork.h"
#include "TResourceManager.h"
#include "TServer.h"
#include "TLuaEngine.h"

#include <algorithm>
#include <chrono>
#include <cctype>

#include <lua.hpp>
#include <openssl/opensslv.h>

using json = nlohmann::json;

namespace {
std::string Lower(std::string Value) {
    std::transform(Value.begin(), Value.end(), Value.begin(), [](unsigned char c) {
        return char(std::tolower(c));
    });
    return Value;
}
}

TControlService::TControlService(TServer& Server, TNetwork& Network, TResourceManager& ResourceManager, TLuaEngine& LuaEngine)
    : mServer(Server)
    , mNetwork(Network)
    , mResourceManager(ResourceManager)
    , mLuaEngine(LuaEngine)
    , mWorker(&TControlService::WorkerMain, this) {
    Application::SetSubsystemStatus("ControlService", Application::Status::Good);
}

TControlService::~TControlService() {
    {
        std::unique_lock lock(mQueueMutex);
        mStopping = true;
    }
    mQueueCV.notify_all();
    if (mWorker.joinable()) {
        mWorker.join();
    }
    Application::SetSubsystemStatus("ControlService", Application::Status::Shutdown);
}

std::future<json> TControlService::Enqueue(std::string Action, json Payload) {
    TRequest Request {
        std::move(Action),
        std::move(Payload),
        {}
    };
    auto Future = Request.Promise.get_future();
    {
        std::unique_lock lock(mQueueMutex);
        mQueue.push(std::move(Request));
    }
    mQueueCV.notify_one();
    return Future;
}

json TControlService::Execute(const std::string& Action, json Payload) {
    return Enqueue(Action, std::move(Payload)).get();
}

void TControlService::WorkerMain() {
    RegisterThread("ControlService");
    while (true) {
        TRequest Request;
        {
            std::unique_lock lock(mQueueMutex);
            mQueueCV.wait(lock, [&] {
                return mStopping || !mQueue.empty();
            });
            if (mStopping && mQueue.empty()) {
                break;
            }
            Request = std::move(mQueue.front());
            mQueue.pop();
        }

        try {
            Request.Promise.set_value(Dispatch(Request.Action, Request.Payload));
        } catch (const std::exception& e) {
            Request.Promise.set_value(Error(Request.Action, e.what()));
        }
    }
}

json TControlService::Dispatch(const std::string& Action, const json& Payload) {
    if (Action == "system.describe_actions") {
        return Success(Action, DescribeActions());
    }
    if (Action == "system.logs.recent") {
        return Success(Action, RecentLogs(Payload));
    }
    if (Action == "system.events.recent") {
        return Success(Action, RecentEvents(Payload));
    }
    if (Action == "server.status") {
        return Success(Action, ServerStatus());
    }
    if (Action == "server.version") {
        return Success(Action, ServerVersion());
    }
    if (Action == "server.subsystems") {
        return Success(Action, ServerSubsystems());
    }
    if (Action == "lua.states.list") {
        return Success(Action, LuaStates());
    }
    if (Action == "players.list") {
        return Success(Action, PlayerList(Payload));
    }
    if (Action == "players.get") {
        return PlayerGet(Payload);
    }
    if (Action == "players.vehicles.list") {
        return PlayerVehicles(Payload);
    }
    if (Action == "players.vehicle_positions") {
        return PlayerVehiclePositions(Payload);
    }
    if (Action == "players.vehicle_position") {
        return PlayerVehiclePosition(Payload);
    }
    if (Action == "players.kick") {
        return PlayerKick(Payload);
    }
    if (Action == "players.disconnect") {
        return PlayersDisconnect(Payload);
    }
    if (Action == "players.find") {
        return PlayerFind(Payload);
    }
    if (Action == "chat.send") {
        return ChatSend(Payload);
    }
    if (Action == "notifications.send") {
        return NotificationSend(Payload);
    }
    if (Action == "dialogs.confirmation") {
        return ConfirmationDialogSend(Payload);
    }
    if (Action == "events.trigger_client") {
        return TriggerClientEvent(Payload);
    }
    if (Action == "spatial.teleport") {
        return SpatialTeleport(Payload);
    }
    if (Action == "spatial.rebase") {
        return SpatialRebase(Payload);
    }
    if (Action == "vehicles.remove") {
        return RemoveVehicle(Payload);
    }
    if (Action == "vehicles.list_all") {
        return Success(Action, VehiclesListAll(Payload));
    }
    if (Action == "vehicles.position_snapshots") {
        return Success(Action, VehiclePositionSnapshots(Payload));
    }
    if (Action == "resources.mods.list") {
        return Success(Action, ModsList());
    }
    if (Action == "resources.mods.reload") {
        return ModsReload();
    }
    if (Action == "resources.mods.set_protected") {
        return ModSetProtected(Payload);
    }
    if (Action == "settings.list") {
        return Success(Action, SettingsList());
    }
    if (Action == "settings.get") {
        return SettingsGet(Payload);
    }
    if (Action == "settings.set") {
        return SettingsSet(Payload);
    }
    return Error(Action, "Unknown control action");
}

json TControlService::DescribeActions() const {
    return json::array({
        { { "action", "system.describe_actions" }, { "description", "Lists currently supported control actions" } },
        { { "action", "system.logs.recent" }, { "description", "Returns recent in-memory console log lines for poll-based controllers" }, { "params", json::array({ "limit", "after_sequence" }) } },
        { { "action", "system.events.recent" }, { "description", "Returns recent structured events for poll-based controllers" }, { "params", json::array({ "limit", "after_sequence" }) } },
        { { "action", "server.status" }, { "description", "Returns server/player/lua summary" } },
        { { "action", "server.version" }, { "description", "Returns server and platform version info" } },
        { { "action", "server.subsystems" }, { "description", "Returns subsystem health/status details" } },
        { { "action", "lua.states.list" }, { "description", "Lists known Lua states and counts" } },
        { { "action", "players.list" }, { "description", "Lists connected players with summary info" }, { "params", json::array({ "include_vehicles" }) } },
        { { "action", "players.get" }, { "description", "Gets one player with identifiers and vehicles" }, { "params", json::array({ "id", "name", "prefix_match" }) } },
        { { "action", "players.vehicles.list" }, { "description", "Lists one player's vehicles with optional raw/parsed position fields" }, { "params", json::array({ "id", "name", "prefix_match", "include_data", "include_position_raw", "include_position_parsed" }) } },
        { { "action", "players.vehicle_positions" }, { "description", "Lists one player's vehicle position snapshots" }, { "params", json::array({ "id", "name", "prefix_match", "include_parsed" }) } },
        { { "action", "players.vehicle_position" }, { "description", "Gets one vehicle position snapshot" }, { "params", json::array({ "player_id", "player_name", "prefix_match", "vehicle_id", "include_parsed" }) } },
        { { "action", "players.kick" }, { "description", "Kicks a player by id or name" }, { "params", json::array({ "id", "name", "reason", "prefix_match" }) } },
        { { "action", "players.disconnect" }, { "description", "Disconnects a player by id or name" }, { "params", json::array({ "id", "name", "reason", "prefix_match" }) } },
        { { "action", "players.find" }, { "description", "Finds players by exact or prefix name match" }, { "params", json::array({ "name", "prefix_match", "limit" }) } },
        { { "action", "chat.send" }, { "description", "Sends server chat globally or to one player" }, { "params", json::array({ "target_id", "message" }) } },
        { { "action", "notifications.send" }, { "description", "Sends a notification globally or to one player" }, { "params", json::array({ "target_id", "message", "icon", "category" }) } },
        { { "action", "dialogs.confirmation" }, { "description", "Sends a confirmation dialog globally or to one player" }, { "params", json::array({ "target_id", "title", "body", "buttons", "interaction_id", "warning", "report_to_server", "report_to_extensions" }) } },
        { { "action", "events.trigger_client" }, { "description", "Triggers a client event globally or for one player" }, { "params", json::array({ "target_id", "event_name", "data" }) } },
        { { "action", "spatial.teleport" }, { "description", "Sends a teleport-oriented client event hook" }, { "params", json::array({ "target_id", "data", "event_name" }) } },
        { { "action", "spatial.rebase" }, { "description", "Sends a rebase-oriented client event hook" }, { "params", json::array({ "target_id", "data", "event_name" }) } },
        { { "action", "vehicles.remove" }, { "description", "Removes one player vehicle" }, { "params", json::array({ "player_id", "vehicle_id" }) } },
        { { "action", "vehicles.list_all" }, { "description", "Lists all vehicles across all players" }, { "params", json::array({ "include_data", "include_position_raw", "include_position_parsed" }) } },
        { { "action", "vehicles.position_snapshots" }, { "description", "Lists all vehicle position snapshots across all players" }, { "params", json::array({ "include_parsed" }) } },
        { { "action", "resources.mods.list" }, { "description", "Lists currently loaded mods/resources" } },
        { { "action", "resources.mods.reload" }, { "description", "Refreshes mod/resource file lists" } },
        { { "action", "resources.mods.set_protected" }, { "description", "Marks a mod protected/unprotected" }, { "params", json::array({ "file_name", "protected" }) } },
        { { "action", "settings.list" }, { "description", "Lists API-visible settings and access modes" } },
        { { "action", "settings.get" }, { "description", "Gets one setting value" }, { "params", json::array({ "category", "key" }) } },
        { { "action", "settings.set" }, { "description", "Sets one writable setting value" }, { "params", json::array({ "category", "key", "value" }) } },
    });
}

json TControlService::RecentLogs(const json& Payload) const {
    const size_t Limit = std::clamp(Payload.value("limit", size_t(100)), size_t(1), size_t(500));
    std::optional<uint64_t> AfterSequence;
    if (Payload.contains("after_sequence")) {
        AfterSequence = Payload.at("after_sequence").get<uint64_t>();
    }

    json Entries = json::array();
    for (const auto& Entry : Application::Console().RecentLogEntries(Limit, AfterSequence)) {
        Entries.push_back({
            { "sequence", Entry.Sequence },
            { "unix_timestamp_ms", Entry.UnixTimestampMs },
            { "line", Entry.Line },
        });
    }

    return {
        { "count", Entries.size() },
        { "limit", Limit },
        { "after_sequence", AfterSequence.has_value() ? json(*AfterSequence) : json(nullptr) },
        { "latest_sequence", Application::Console().LatestLogSequence() },
        { "entries", std::move(Entries) },
    };
}

json TControlService::RecentEvents(const json& Payload) const {
    const size_t Limit = std::clamp(Payload.value("limit", size_t(100)), size_t(1), size_t(500));
    std::optional<uint64_t> AfterSequence;
    if (Payload.contains("after_sequence")) {
        AfterSequence = Payload.at("after_sequence").get<uint64_t>();
    }

    json Entries = json::array();
    for (const auto& Entry : Application::Console().RecentEventEntries(Limit, AfterSequence)) {
        Entries.push_back({
            { "sequence", Entry.Sequence },
            { "unix_timestamp_ms", Entry.UnixTimestampMs },
            { "type", Entry.Type },
            { "category", Entry.Category },
            { "data", Entry.Data },
        });
    }

    return {
        { "count", Entries.size() },
        { "limit", Limit },
        { "after_sequence", AfterSequence.has_value() ? json(*AfterSequence) : json(nullptr) },
        { "latest_sequence", Application::Console().LatestEventSequence() },
        { "events", std::move(Entries) },
    };
}

json TControlService::ServerStatus() const {
    size_t CarCount = 0;
    size_t ConnectedCount = 0;
    size_t GuestCount = 0;
    size_t SyncedCount = 0;
    size_t SyncingCount = 0;
    size_t MissedPacketQueueSum = 0;
    int LargestSecondsSinceLastPing = 0;
    mServer.ForEachClient([&](std::weak_ptr<TClient> Client) -> bool {
        if (!Client.expired()) {
            auto Locked = Client.lock();
            CarCount += Locked->GetCarCount();
            ConnectedCount += Locked->IsUDPConnected() ? 1 : 0;
            GuestCount += Locked->IsGuest() ? 1 : 0;
            SyncedCount += Locked->IsSynced() ? 1 : 0;
            SyncingCount += Locked->IsSyncing() ? 1 : 0;
            MissedPacketQueueSum += Locked->MissedPacketQueueSize();
            LargestSecondsSinceLastPing = std::max(LargestSecondsSinceLastPing, Locked->SecondsSinceLastPing());
        }
        return true;
    });

    return {
        { "players", {
              { "total", mServer.ClientCount() },
              { "udp_connected", ConnectedCount },
              { "guests", GuestCount },
              { "synced", SyncedCount },
              { "syncing", SyncingCount },
          } },
        { "cars", CarCount },
        { "largest_seconds_since_last_ping", LargestSecondsSinceLastPing },
        { "missed_packet_queue_sum", MissedPacketQueueSum },
        { "uptime_ms", mServer.UptimeTimer.GetElapsedTime() },
        { "pps", Application::PPS() },
        { "lua", {
              { "queued_results_to_check", mLuaEngine.GetResultsToCheckSize() },
              { "states", mLuaEngine.GetLuaStateCount() },
              { "event_timers", mLuaEngine.GetTimedEventsCount() },
              { "event_handlers", mLuaEngine.GetRegisteredEventHandlerCount() },
          } },
        { "subsystems", ServerSubsystems() },
    };
}

json TControlService::ServerVersion() const {
    std::string Platform = "Unknown";
#if defined(BEAMMP_WINDOWS)
    Platform = "Windows";
#elif defined(BEAMMP_LINUX)
    Platform = "Linux";
#elif defined(BEAMMP_FREEBSD)
    Platform = "FreeBSD";
#elif defined(BEAMMP_APPLE)
    Platform = "Apple";
#endif
    return {
        { "platform", Platform },
        { "server", Application::ServerVersionString() },
        { "lua", fmt::format("{}.{}.{}", LUA_VERSION_MAJOR, LUA_VERSION_MINOR, LUA_VERSION_RELEASE) },
        { "openssl", fmt::format("{}.{}.{}", OPENSSL_VERSION_MAJOR, OPENSSL_VERSION_MINOR, OPENSSL_VERSION_PATCH) },
    };
}

json TControlService::ServerSubsystems() const {
    json Counts = {
        { "starting", 0 },
        { "good", 0 },
        { "bad", 0 },
        { "shutting_down", 0 },
        { "shutdown", 0 },
    };
    json Systems = json::array();
    auto Statuses = Application::GetSubsystemStatuses();
    for (const auto& [Name, Status] : Statuses) {
        std::string StatusName;
        switch (Status) {
        case Application::Status::Starting:
            StatusName = "starting";
            break;
        case Application::Status::Good:
            StatusName = "good";
            break;
        case Application::Status::Bad:
            StatusName = "bad";
            break;
        case Application::Status::ShuttingDown:
            StatusName = "shutting_down";
            break;
        case Application::Status::Shutdown:
            StatusName = "shutdown";
            break;
        default:
            beammp_assert_not_reachable();
        }
        Counts[StatusName] = Counts[StatusName].get<size_t>() + 1;
        Systems.push_back({
            { "name", Name },
            { "status", StatusName },
        });
    }
    return {
        { "counts", Counts },
        { "systems", Systems },
    };
}

json TControlService::LuaStates() const {
    return {
        { "count", mLuaEngine.GetLuaStateCount() },
        { "states", mLuaEngine.GetLuaStateNames() },
    };
}

json TControlService::PlayerList(const json& Payload) const {
    const bool IncludeVehicles = Payload.value("include_vehicles", false);
    json Players = json::array();
    mServer.ForEachClient([&](std::weak_ptr<TClient> Client) -> bool {
        if (!Client.expired()) {
            Players.push_back(SerializeClient(Client.lock(), IncludeVehicles));
        }
        return true;
    });
    return {
        { "include_vehicles", IncludeVehicles },
        { "count", Players.size() },
        { "players", Players },
    };
}

json TControlService::PlayerGet(const json& Payload) const {
    auto Client = FindClient(Payload, "players.get");
    if (!Client) {
        return Error("players.get", "Player not found");
    }
    return Success("players.get", SerializeClient(Client, true));
}

json TControlService::PlayerVehicles(const json& Payload) const {
    auto Client = FindClient(Payload, "players.vehicles.list");
    if (!Client) {
        return Error("players.vehicles.list", "Player not found");
    }

    const bool IncludeData = Payload.value("include_data", true);
    const bool IncludePositionRaw = Payload.value("include_position_raw", true);
    const bool IncludePositionParsed = Payload.value("include_position_parsed", false);

    json Vehicles = json::array();
    auto LockedData = Client->GetAllCars();
    for (const auto& Vehicle : *LockedData.VehicleData) {
        Vehicles.push_back(SerializeVehicle(Client, Vehicle, IncludeData, IncludePositionRaw, IncludePositionParsed));
    }

    return Success("players.vehicles.list", {
        { "player", {
              { "id", Client->GetID() },
              { "name", Client->GetName() },
          } },
        { "count", Vehicles.size() },
        { "include_data", IncludeData },
        { "include_position_raw", IncludePositionRaw },
        { "include_position_parsed", IncludePositionParsed },
        { "vehicles", std::move(Vehicles) },
    });
}

json TControlService::PlayerVehiclePositions(const json& Payload) const {
    const bool IncludeParsed = Payload.value("include_parsed", true);
    if (!Payload.contains("id") && !Payload.contains("name") && !Payload.contains("player_id") && !Payload.contains("player_name")) {
        json Players = json::array();
        mServer.ForEachClient([&](std::weak_ptr<TClient> WeakClient) -> bool {
            if (WeakClient.expired()) {
                return true;
            }
            auto Client = WeakClient.lock();
            json Positions = json::array();
            auto LockedData = Client->GetAllCars();
            for (const auto& Vehicle : *LockedData.VehicleData) {
                Positions.push_back(SerializeVehiclePosition(Client, Vehicle.ID(), Client->GetCarPositionRaw(Vehicle.ID()), IncludeParsed));
            }
            Players.push_back({
                { "player", {
                      { "id", Client->GetID() },
                      { "name", Client->GetName() },
                  } },
                { "count", Positions.size() },
                { "vehicles", std::move(Positions) },
            });
            return true;
        });

        return Success("players.vehicle_positions", {
            { "scope", "all_players" },
            { "include_parsed", IncludeParsed },
            { "players", std::move(Players) },
        });
    }

    auto Client = FindClient(Payload, "players.vehicle_positions");
    if (!Client) {
        return Error("players.vehicle_positions", "Player not found");
    }

    json Positions = json::array();
    auto LockedData = Client->GetAllCars();
    for (const auto& Vehicle : *LockedData.VehicleData) {
        Positions.push_back(SerializeVehiclePosition(Client, Vehicle.ID(), Client->GetCarPositionRaw(Vehicle.ID()), IncludeParsed));
    }

    return Success("players.vehicle_positions", {
        { "scope", "single_player" },
        { "player", {
              { "id", Client->GetID() },
              { "name", Client->GetName() },
          } },
        { "count", Positions.size() },
        { "include_parsed", IncludeParsed },
        { "vehicles", std::move(Positions) },
    });
}

json TControlService::PlayerVehiclePosition(const json& Payload) const {
    if (!Payload.contains("vehicle_id")) {
        return Error("players.vehicle_position", "Expected 'vehicle_id'");
    }

    json LookupPayload = Payload;
    if (LookupPayload.contains("player_id")) {
        LookupPayload["id"] = LookupPayload.at("player_id");
    }
    if (LookupPayload.contains("player_name")) {
        LookupPayload["name"] = LookupPayload.at("player_name");
    }

    auto Client = FindClient(LookupPayload, "players.vehicle_position");
    if (!Client) {
        return Error("players.vehicle_position", "Player not found");
    }

    const int VehicleID = Payload.at("vehicle_id").get<int>();
    const std::string PositionRaw = Client->GetCarPositionRaw(VehicleID);
    if (PositionRaw.empty()) {
        return Error("players.vehicle_position", "Vehicle position not found");
    }

    return Success("players.vehicle_position", {
        { "player", {
              { "id", Client->GetID() },
              { "name", Client->GetName() },
          } },
        { "vehicle", SerializeVehiclePosition(Client, VehicleID, PositionRaw, Payload.value("include_parsed", true)) },
    });
}

json TControlService::PlayerKick(const json& Payload) {
    auto Client = FindClient(Payload, "players.kick");
    if (!Client) {
        return Error("players.kick", "Player not found");
    }
    const std::string Reason = Payload.value("reason", std::string("Kicked by control service"));
    const auto Snapshot = SerializeClient(Client, false);
    mNetwork.ClientKick(*Client, Reason);
    Application::Console().RecordEvent("player", "kick", {
        { "player", Snapshot },
        { "reason", Reason },
    });
    return Success("players.kick", {
        { "player", Snapshot },
        { "reason", Reason },
    });
}

json TControlService::PlayersDisconnect(const json& Payload) {
    auto Client = FindClient(Payload, "players.disconnect");
    if (!Client) {
        return Error("players.disconnect", "Player not found");
    }
    const std::string Reason = Payload.value("reason", std::string("Disconnected by control service"));
    const auto Snapshot = SerializeClient(Client, false);
    Client->Disconnect(Reason);
    Application::Console().RecordEvent("player", "disconnect_request", {
        { "player", Snapshot },
        { "reason", Reason },
    });
    return Success("players.disconnect", {
        { "player", Snapshot },
        { "reason", Reason },
    });
}

json TControlService::PlayerFind(const json& Payload) const {
    if (!Payload.contains("name")) {
        return Error("players.find", "Expected 'name'");
    }
    const auto Query = Lower(Payload.at("name").get<std::string>());
    const bool PrefixMatch = Payload.value("prefix_match", true);
    const size_t Limit = std::clamp(Payload.value("limit", size_t(25)), size_t(1), size_t(250));

    json Players = json::array();
    mServer.ForEachClient([&](std::weak_ptr<TClient> WeakClient) -> bool {
        if (WeakClient.expired()) {
            return true;
        }
        auto Client = WeakClient.lock();
        const auto Name = Lower(Client->GetName());
        const bool Match = PrefixMatch ? Name.starts_with(Query) : Name == Query;
        if (Match) {
            Players.push_back(SerializeClient(Client, false));
            if (Players.size() >= Limit) {
                return false;
            }
        }
        return true;
    });

    return Success("players.find", {
        { "query", Payload.at("name") },
        { "prefix_match", PrefixMatch },
        { "count", Players.size() },
        { "players", std::move(Players) },
    });
}

json TControlService::ChatSend(const json& Payload) {
    if (!Payload.contains("message")) {
        return Error("chat.send", "Expected 'message'");
    }
    const int TargetID = Payload.value("target_id", -1);
    const auto Result = LuaAPI::MP::SendChatMessage(TargetID, Payload.at("message").get<std::string>());
    if (!Result.first) {
        return Error("chat.send", Result.second);
    }
    Application::Console().RecordEvent("control", "chat.send", {
        { "target_id", TargetID },
        { "message", Payload.at("message") },
    });
    return Success("chat.send", {
        { "target_id", TargetID },
    });
}

json TControlService::NotificationSend(const json& Payload) {
    if (!Payload.contains("message")) {
        return Error("notifications.send", "Expected 'message'");
    }
    const int TargetID = Payload.value("target_id", -1);
    const auto Result = LuaAPI::MP::SendNotification(
        TargetID,
        Payload.at("message").get<std::string>(),
        Payload.value("icon", std::string("info")),
        Payload.value("category", std::string("general")));
    if (!Result.first) {
        return Error("notifications.send", Result.second);
    }
    Application::Console().RecordEvent("control", "notifications.send", {
        { "target_id", TargetID },
        { "message", Payload.at("message") },
        { "icon", Payload.value("icon", std::string("info")) },
        { "category", Payload.value("category", std::string("general")) },
    });
    return Success("notifications.send", {
        { "target_id", TargetID },
    });
}

json TControlService::ConfirmationDialogSend(const json& Payload) {
    if (!Payload.contains("title") || !Payload.contains("body") || !Payload.contains("buttons")) {
        return Error("dialogs.confirmation", "Expected 'title', 'body', and 'buttons'");
    }

    const int TargetID = Payload.value("target_id", -1);
    const json PacketBody = {
        { "title", Payload.at("title") },
        { "body", Payload.at("body") },
        { "buttons", Payload.at("buttons") },
        { "interactionID", Payload.value("interaction_id", std::string("http-api")) },
        { "class", Payload.value("warning", false) ? "experimental" : "" },
        { "reportToServer", Payload.value("report_to_server", true) },
        { "reportToExtensions", Payload.value("report_to_extensions", true) },
    };

    const std::string Packet = "D" + PacketBody.dump();
    if (TargetID == -1) {
        mNetwork.SendToAll(nullptr, StringToVector(Packet), true, true);
        Application::Console().RecordEvent("control", "dialogs.confirmation", {
            { "target_id", TargetID },
            { "interaction_id", PacketBody.at("interactionID") },
            { "title", PacketBody.at("title") },
        });
        return Success("dialogs.confirmation", {
            { "target_id", TargetID },
            { "interaction_id", PacketBody.at("interactionID") },
        });
    }

    auto Client = FindClientByID(TargetID);
    if (!Client) {
        return Error("dialogs.confirmation", "Player not found");
    }
    if (!Client->IsSynced()) {
        return Error("dialogs.confirmation", "Player is not synced yet");
    }
    if (!mNetwork.Respond(*Client, StringToVector(Packet), true)) {
        return Error("dialogs.confirmation", "Failed to send packet");
    }
    Application::Console().RecordEvent("control", "dialogs.confirmation", {
        { "target_id", TargetID },
        { "interaction_id", PacketBody.at("interactionID") },
        { "title", PacketBody.at("title") },
    });
    return Success("dialogs.confirmation", {
        { "target_id", TargetID },
        { "interaction_id", PacketBody.at("interactionID") },
    });
}

json TControlService::TriggerClientEvent(const json& Payload) {
    return TriggerClientEventAction("events.trigger_client", Payload);
}

json TControlService::TriggerClientEventAction(const std::string& Action, const json& Payload) {
    if (!Payload.contains("event_name")) {
        return Error(Action, "Expected 'event_name'");
    }
    const int TargetID = Payload.value("target_id", -1);
    const std::string Packet = "E:" + Payload.at("event_name").get<std::string>() + ":" + Payload.value("data", std::string(""));
    if (TargetID == -1) {
        mNetwork.SendToAll(nullptr, StringToVector(Packet), true, true);
        Application::Console().RecordEvent("control", Action, {
            { "target_id", TargetID },
            { "event_name", Payload.at("event_name") },
            { "data", Payload.value("data", std::string("")) },
        });
        return Success(Action, {
            { "target_id", TargetID },
        });
    }

    auto Client = FindClientByID(TargetID);
    if (!Client) {
        return Error(Action, "Player not found");
    }
    if (!Client->IsSyncing() && !Client->IsSynced()) {
        return Error(Action, "Player hasn't joined yet");
    }
    if (!mNetwork.Respond(*Client, StringToVector(Packet), true)) {
        mNetwork.ClientKick(*Client, "Disconnected after failing to receive packets");
        return Error(Action, "Failed to send event packet");
    }
    Application::Console().RecordEvent("control", Action, {
        { "target_id", TargetID },
        { "event_name", Payload.at("event_name") },
        { "data", Payload.value("data", std::string("")) },
    });
    return Success(Action, {
        { "target_id", TargetID },
    });
}

json TControlService::SpatialTeleport(const json& Payload) {
    json EventPayload = Payload;
    EventPayload["event_name"] = Payload.value("event_name", std::string("BeamMPSpatialTeleport"));
    EventPayload["data"] = Payload.value("data", json::object()).dump();
    return TriggerClientEventAction("spatial.teleport", EventPayload);
}

json TControlService::SpatialRebase(const json& Payload) {
    json EventPayload = Payload;
    EventPayload["event_name"] = Payload.value("event_name", std::string("BeamMPSpatialRebase"));
    EventPayload["data"] = Payload.value("data", json::object()).dump();
    return TriggerClientEventAction("spatial.rebase", EventPayload);
}

json TControlService::RemoveVehicle(const json& Payload) {
    if (!Payload.contains("player_id") || !Payload.contains("vehicle_id")) {
        return Error("vehicles.remove", "Expected 'player_id' and 'vehicle_id'");
    }
    const auto Result = LuaAPI::MP::RemoveVehicle(Payload.at("player_id").get<int>(), Payload.at("vehicle_id").get<int>());
    if (!Result.first) {
        return Error("vehicles.remove", Result.second);
    }
    Application::Console().RecordEvent("vehicle", "remove", {
        { "player_id", Payload.at("player_id") },
        { "vehicle_id", Payload.at("vehicle_id") },
    });
    return Success("vehicles.remove", {
        { "player_id", Payload.at("player_id") },
        { "vehicle_id", Payload.at("vehicle_id") },
    });
}

json TControlService::VehiclesListAll(const json& Payload) const {
    const bool IncludeData = Payload.value("include_data", true);
    const bool IncludePositionRaw = Payload.value("include_position_raw", true);
    const bool IncludePositionParsed = Payload.value("include_position_parsed", false);
    json Vehicles = json::array();
    mServer.ForEachClient([&](std::weak_ptr<TClient> WeakClient) -> bool {
        if (WeakClient.expired()) {
            return true;
        }
        auto Client = WeakClient.lock();
        auto LockedData = Client->GetAllCars();
        for (const auto& Vehicle : *LockedData.VehicleData) {
            auto Item = SerializeVehicle(Client, Vehicle, IncludeData, IncludePositionRaw, IncludePositionParsed);
            Item["player_id"] = Client->GetID();
            Item["player_name"] = Client->GetName();
            Vehicles.push_back(std::move(Item));
        }
        return true;
    });
    return {
        { "count", Vehicles.size() },
        { "vehicles", std::move(Vehicles) },
    };
}

json TControlService::VehiclePositionSnapshots(const json& Payload) const {
    const bool IncludeParsed = Payload.value("include_parsed", true);
    json Vehicles = json::array();
    mServer.ForEachClient([&](std::weak_ptr<TClient> WeakClient) -> bool {
        if (WeakClient.expired()) {
            return true;
        }
        auto Client = WeakClient.lock();
        auto LockedData = Client->GetAllCars();
        for (const auto& Vehicle : *LockedData.VehicleData) {
            Vehicles.push_back(SerializeVehiclePosition(Client, Vehicle.ID(), Client->GetCarPositionRaw(Vehicle.ID()), IncludeParsed));
        }
        return true;
    });
    return {
        { "count", Vehicles.size() },
        { "include_parsed", IncludeParsed },
        { "vehicles", std::move(Vehicles) },
    };
}

json TControlService::ModsList() const {
    return {
        { "mods", mResourceManager.GetMods() },
        { "mods_loaded", mResourceManager.ModsLoaded() },
        { "max_mod_size", mResourceManager.MaxModSize() },
    };
}

json TControlService::ModsReload() {
    mResourceManager.RefreshFiles();
    Application::Console().RecordEvent("resource", "mods.reload", {});
    return Success("resources.mods.reload", ModsList());
}

json TControlService::ModSetProtected(const json& Payload) {
    if (!Payload.contains("file_name") || !Payload.contains("protected")) {
        return Error("resources.mods.set_protected", "Expected 'file_name' and 'protected'");
    }
    const std::string FileName = Payload.at("file_name").get<std::string>();
    bool Found = false;
    for (const auto& Mod : mResourceManager.GetMods()) {
        if (Mod["file_name"].get<std::string>() == FileName) {
            Found = true;
            break;
        }
    }
    if (!Found) {
        return Error("resources.mods.set_protected", "Mod not found");
    }
    const bool Protected = Payload.at("protected").get<bool>();
    mResourceManager.SetProtected(FileName, Protected);
    Application::Console().RecordEvent("resource", "mods.set_protected", {
        { "file_name", FileName },
        { "protected", Protected },
    });
    return Success("resources.mods.set_protected", {
        { "file_name", FileName },
        { "protected", Protected },
    });
}

json TControlService::SettingsList() const {
    json SettingsList = json::array();
    for (const auto& [ComposedKey, ACL] : Application::Settings.getAccessControlMap()) {
        if (ACL.second == Settings::SettingsAccessMask::NO_ACCESS) {
            continue;
        }
        SettingsList.push_back({
            { "category", ComposedKey.Category },
            { "key", ComposedKey.Key },
            { "access", AccessMaskToString(ACL.second) },
            { "value", SerializeSettingsValue(Application::Settings.get(ACL.first)) },
        });
    }
    return {
        { "settings", SettingsList },
    };
}

json TControlService::SettingsGet(const json& Payload) const {
    if (!Payload.contains("category") || !Payload.contains("key")) {
        return Error("settings.get", "Expected 'category' and 'key'");
    }
    const ComposedKey Key {
        Payload.at("category").get<std::string>(),
        Payload.at("key").get<std::string>(),
    };
    try {
        const auto ACL = Application::Settings.getConsoleInputAccessMapping(Key);
        if (ACL.second == Settings::SettingsAccessMask::NO_ACCESS) {
            return Error("settings.get", "Setting is not API-visible");
        }
        return Success("settings.get", {
            { "category", Key.Category },
            { "key", Key.Key },
            { "access", AccessMaskToString(ACL.second) },
            { "value", SerializeSettingsValue(Application::Settings.get(ACL.first)) },
        });
    } catch (const std::logic_error& e) {
        return Error("settings.get", e.what());
    }
}

json TControlService::SettingsSet(const json& Payload) {
    if (!Payload.contains("category") || !Payload.contains("key") || !Payload.contains("value")) {
        return Error("settings.set", "Expected 'category', 'key', and 'value'");
    }
    const ComposedKey Key {
        Payload.at("category").get<std::string>(),
        Payload.at("key").get<std::string>(),
    };
    try {
        const auto ACL = Application::Settings.getConsoleInputAccessMapping(Key);
        if (ACL.second != Settings::SettingsAccessMask::READ_WRITE) {
            return Error("settings.set", "Setting is not writable");
        }
        const auto Current = Application::Settings.get(ACL.first);
        if (std::holds_alternative<std::string>(Current)) {
            Application::Settings.setConsoleInputAccessMapping(Key, Payload.at("value").get<std::string>());
        } else if (std::holds_alternative<int>(Current)) {
            Application::Settings.setConsoleInputAccessMapping(Key, Payload.at("value").get<int>());
        } else if (std::holds_alternative<bool>(Current)) {
            Application::Settings.setConsoleInputAccessMapping(Key, Payload.at("value").get<bool>());
        }
        Application::Console().RecordEvent("settings", "set", {
            { "category", Key.Category },
            { "key", Key.Key },
            { "value", Payload.at("value") },
        });
        return Success("settings.set", {
            { "category", Key.Category },
            { "key", Key.Key },
            { "value", SerializeSettingsValue(Application::Settings.get(ACL.first)) },
        });
    } catch (const std::exception& e) {
        return Error("settings.set", e.what());
    }
}

json TControlService::SerializeClient(const std::shared_ptr<TClient>& Client, bool IncludeVehicles) const {
    json Out {
        { "id", Client->GetID() },
        { "name", Client->GetName() },
        { "roles", Client->GetRoles() },
        { "guest", Client->IsGuest() },
        { "synced", Client->IsSynced() },
        { "syncing", Client->IsSyncing() },
        { "udp_connected", Client->IsUDPConnected() },
        { "disconnected", Client->IsDisconnected() },
        { "cars", Client->GetCarCount() },
        { "unicycle_id", Client->GetUnicycleID() },
        { "seconds_since_last_ping", Client->SecondsSinceLastPing() },
        { "missed_packet_queue", Client->MissedPacketQueueSize() },
        { "identifiers", Client->GetIdentifiers() },
    };

    if (IncludeVehicles) {
        json Vehicles = json::array();
        auto LockedData = Client->GetAllCars();
        for (const auto& Vehicle : *LockedData.VehicleData) {
            Vehicles.push_back(SerializeVehicle(Client, Vehicle, true, true, false));
        }
        Out["vehicles"] = std::move(Vehicles);
    }

    return Out;
}

json TControlService::SerializeVehicle(const std::shared_ptr<TClient>& Client, const TVehicleData& Vehicle, bool IncludeData, bool IncludePositionRaw, bool IncludePositionParsed) const {
    json Out {
        { "id", Vehicle.ID() },
    };

    if (IncludeData) {
        Out["data"] = Vehicle.Data();
    }

    const std::string PositionRaw = Client->GetCarPositionRaw(Vehicle.ID());
    if (IncludePositionRaw) {
        Out["position_raw"] = PositionRaw;
    }
    if (IncludePositionParsed) {
        Out["position"] = ParseRawPosition(PositionRaw);
    }

    return Out;
}

json TControlService::SerializeVehiclePosition(const std::shared_ptr<TClient>& Client, int VehicleID, const std::string& PositionRaw, bool IncludeParsed) const {
    json Out {
        { "player_id", Client->GetID() },
        { "player_name", Client->GetName() },
        { "vehicle_id", VehicleID },
        { "position_raw", PositionRaw },
    };
    if (IncludeParsed) {
        Out["position"] = ParseRawPosition(PositionRaw);
    }
    return Out;
}

json TControlService::ParseRawPosition(const std::string& PositionRaw) const {
    if (PositionRaw.empty()) {
        return nullptr;
    }
    auto Parsed = json::parse(PositionRaw, nullptr, false);
    if (Parsed.is_discarded()) {
        return nullptr;
    }
    return Parsed;
}

std::shared_ptr<TClient> TControlService::FindClient(const json& Payload, const std::string& Action) const {
    if (Payload.contains("id")) {
        return FindClientByID(Payload.at("id").get<int>());
    }
    if (Payload.contains("name")) {
        return FindClientByName(Payload.at("name").get<std::string>(), Payload.value("prefix_match", true));
    }
    if (Payload.contains("player_id")) {
        return FindClientByID(Payload.at("player_id").get<int>());
    }
    if (Payload.contains("player_name")) {
        return FindClientByName(Payload.at("player_name").get<std::string>(), Payload.value("prefix_match", true));
    }
    beammp_debugf("{} missing player selector", Action);
    return nullptr;
}

std::shared_ptr<TClient> TControlService::FindClientByID(int ID) const {
    auto MaybeClient = GetClient(mServer, ID);
    if (!MaybeClient || MaybeClient->expired()) {
        return nullptr;
    }
    return MaybeClient->lock();
}

std::shared_ptr<TClient> TControlService::FindClientByName(std::string Name, bool PrefixMatch) const {
    const auto Needle = Lower(std::move(Name));
    std::shared_ptr<TClient> Result;
    mServer.ForEachClient([&](std::weak_ptr<TClient> Client) -> bool {
        if (Client.expired()) {
            return true;
        }
        auto Locked = Client.lock();
        const auto Candidate = Lower(Locked->GetName());
        const bool Match = PrefixMatch
            ? (Candidate.starts_with(Needle) || Needle.starts_with(Candidate))
            : Candidate == Needle;
        if (Match) {
            Result = Locked;
            return false;
        }
        return true;
    });
    return Result;
}

json TControlService::SerializeSettingsValue(const Settings::SettingsTypeVariant& Value) {
    return std::visit([](const auto& Inner) -> json {
        return Inner;
    }, Value);
}

std::string TControlService::AccessMaskToString(Settings::SettingsAccessMask Mask) {
    switch (Mask) {
    case Settings::SettingsAccessMask::READ_ONLY:
        return "read_only";
    case Settings::SettingsAccessMask::READ_WRITE:
        return "read_write";
    case Settings::SettingsAccessMask::NO_ACCESS:
        return "no_access";
    default:
        beammp_assert_not_reachable();
    }
}

json TControlService::Success(const std::string& Action, json Data) const {
    return {
        { "ok", true },
        { "action", Action },
        { "data", std::move(Data) },
    };
}

json TControlService::Error(const std::string& Action, const std::string& Message) const {
    return {
        { "ok", false },
        { "action", Action },
        { "error", Message },
    };
}
