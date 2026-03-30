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

#pragma once

#include "Settings.h"

#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

class TClient;
class TNetwork;
class TResourceManager;
class TServer;
class TLuaEngine;
class TVehicleData;

class TControlService final {
public:
    using json = nlohmann::json;

    TControlService(TServer& Server, TNetwork& Network, TResourceManager& ResourceManager, TLuaEngine& LuaEngine);
    ~TControlService();

    TControlService(const TControlService&) = delete;
    TControlService& operator=(const TControlService&) = delete;

    [[nodiscard]] std::future<json> Enqueue(std::string Action, json Payload = {});
    [[nodiscard]] json Execute(const std::string& Action, json Payload = {});

private:
    struct TRequest {
        std::string Action;
        json Payload;
        std::promise<json> Promise;
    };

    void WorkerMain();
    [[nodiscard]] json Dispatch(const std::string& Action, const json& Payload);
    [[nodiscard]] json DescribeActions() const;
    [[nodiscard]] json RecentLogs(const json& Payload) const;
    [[nodiscard]] json RecentEvents(const json& Payload) const;
    [[nodiscard]] json ServerStatus() const;
    [[nodiscard]] json ServerVersion() const;
    [[nodiscard]] json ServerSubsystems() const;
    [[nodiscard]] json LuaStates() const;
    [[nodiscard]] json PlayerList(const json& Payload) const;
    [[nodiscard]] json PlayerGet(const json& Payload) const;
    [[nodiscard]] json PlayerVehicles(const json& Payload) const;
    [[nodiscard]] json PlayerVehiclePositions(const json& Payload) const;
    [[nodiscard]] json PlayerVehiclePosition(const json& Payload) const;
    [[nodiscard]] json PlayerKick(const json& Payload);
    [[nodiscard]] json ChatSend(const json& Payload);
    [[nodiscard]] json NotificationSend(const json& Payload);
    [[nodiscard]] json ConfirmationDialogSend(const json& Payload);
    [[nodiscard]] json TriggerClientEvent(const json& Payload);
    [[nodiscard]] json TriggerClientEventAction(const std::string& Action, const json& Payload);
    [[nodiscard]] json RemoveVehicle(const json& Payload);
    [[nodiscard]] json VehiclesListAll(const json& Payload) const;
    [[nodiscard]] json VehiclePositionSnapshots(const json& Payload) const;
    [[nodiscard]] json PlayerFind(const json& Payload) const;
    [[nodiscard]] json PlayersDisconnect(const json& Payload);
    [[nodiscard]] json SpatialTeleport(const json& Payload);
    [[nodiscard]] json SpatialRebase(const json& Payload);
    [[nodiscard]] json ModsList() const;
    [[nodiscard]] json ModsReload();
    [[nodiscard]] json ModSetProtected(const json& Payload);
    [[nodiscard]] json SettingsList() const;
    [[nodiscard]] json SettingsGet(const json& Payload) const;
    [[nodiscard]] json SettingsSet(const json& Payload);

    [[nodiscard]] json SerializeClient(const std::shared_ptr<TClient>& Client, bool IncludeVehicles) const;
    [[nodiscard]] json SerializeVehicle(const std::shared_ptr<TClient>& Client, const TVehicleData& Vehicle, bool IncludeData, bool IncludePositionRaw, bool IncludePositionParsed) const;
    [[nodiscard]] json SerializeVehiclePosition(const std::shared_ptr<TClient>& Client, int VehicleID, const std::string& PositionRaw, bool IncludeParsed) const;
    [[nodiscard]] json ParseRawPosition(const std::string& PositionRaw) const;
    [[nodiscard]] std::shared_ptr<TClient> FindClient(const json& Payload, const std::string& Action) const;
    [[nodiscard]] std::shared_ptr<TClient> FindClientByID(int ID) const;
    [[nodiscard]] std::shared_ptr<TClient> FindClientByName(std::string Name, bool PrefixMatch) const;
    [[nodiscard]] static json SerializeSettingsValue(const Settings::SettingsTypeVariant& Value);
    [[nodiscard]] static std::string AccessMaskToString(Settings::SettingsAccessMask Mask);
    [[nodiscard]] json Success(const std::string& Action, json Data = {}) const;
    [[nodiscard]] json Error(const std::string& Action, const std::string& Message) const;

    TServer& mServer;
    TNetwork& mNetwork;
    TResourceManager& mResourceManager;
    TLuaEngine& mLuaEngine;
    std::thread mWorker;
    std::mutex mQueueMutex;
    std::condition_variable mQueueCV;
    std::queue<TRequest> mQueue;
    bool mStopping = false;
};
