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

#include "TServer.h"
#include "Client.h"
#include "Common.h"
#include "CustomAssert.h"
#include "TLuaEngine.h"
#include "TNetwork.h"
#include "TPPSMonitor.h"
#include <TLuaPlugin.h>
#include <algorithm>
#include <any>
#include <cmath>
#include <optional>
#include <sstream>
#include <utility>

#include <nlohmann/json.hpp>

#include "LuaAPI.h"

#undef GetObject // Fixes Windows

#include "Json.h"

namespace {
using json = nlohmann::json;
using SpatialOffset = TClient::TSpatialOffset;

json PlayerEventSnapshot(TClient& Client) {
    return {
        { "id", Client.GetID() },
        { "name", Client.GetName() },
    };
}

std::optional<SpatialOffset> ParseSpatialOffsetFromJson(const json& Payload) {
    if (Payload.contains("offset") && Payload.at("offset").is_array() && Payload.at("offset").size() >= 3) {
        return SpatialOffset {
            Payload.at("offset").at(0).get<double>(),
            Payload.at("offset").at(1).get<double>(),
            Payload.at("offset").at(2).get<double>(),
        };
    }

    if (Payload.contains("offset") && Payload.at("offset").is_object()) {
        const auto& Offset = Payload.at("offset");
        if (Offset.contains("x") && Offset.contains("y") && Offset.contains("z")) {
            return SpatialOffset {
                Offset.at("x").get<double>(),
                Offset.at("y").get<double>(),
                Offset.at("z").get<double>(),
            };
        }
    }

    if (Payload.contains("x") && Payload.contains("y") && Payload.contains("z")) {
        return SpatialOffset {
            Payload.at("x").get<double>(),
            Payload.at("y").get<double>(),
            Payload.at("z").get<double>(),
        };
    }

    return std::nullopt;
}

json SerializeSpatialOffset(const SpatialOffset& Offset) {
    return {
        { "x", Offset[0] },
        { "y", Offset[1] },
        { "z", Offset[2] },
    };
}

std::optional<SpatialOffset> ParseLocalPositionFromRawPacket(const std::string& Data) {
    auto Payload = json::parse(Data, nullptr, false);
    if (Payload.is_discarded() || !Payload.is_object() || !Payload.contains("pos") || !Payload.at("pos").is_array() || Payload.at("pos").size() < 3) {
        return std::nullopt;
    }

    return SpatialOffset {
        Payload.at("pos").at(0).get<double>(),
        Payload.at("pos").at(1).get<double>(),
        Payload.at("pos").at(2).get<double>(),
    };
}

TClient::TAutoRebaseThresholdBias ClearRecoveredThresholdBias(
    const TClient::TAutoRebaseThresholdBias& CurrentBias,
    const SpatialOffset& LocalPosition,
    int SafeLimit,
    int RetriggerBand) {
    auto UpdatedBias = CurrentBias;
    const auto ClearLimit = std::max(0, SafeLimit - RetriggerBand);
    for (size_t Axis = 0; Axis < 2; ++Axis) {
        if (std::abs(LocalPosition[Axis]) <= static_cast<double>(ClearLimit)) {
            UpdatedBias[Axis] = 0;
        }
    }

    UpdatedBias[2] = 0;
    return UpdatedBias;
}

struct AutomaticRebaseDecision {
    SpatialOffset Offset;
    TClient::TAutoRebaseThresholdBias ThresholdBias;
};

std::optional<AutomaticRebaseDecision> EvaluateAutomaticRebase(
    const SpatialOffset& CurrentOffset,
    const SpatialOffset& LocalPosition,
    const TClient::TAutoRebaseThresholdBias& CurrentBias,
    int SafeLimit,
    int RetriggerBand) {
    if (SafeLimit <= 0) {
        return std::nullopt;
    }

    const auto WrapWidth = static_cast<double>(SafeLimit) * 2.0;
    if (WrapWidth <= 0.0) {
        return std::nullopt;
    }

    auto NextOffset = CurrentOffset;
    auto NextBias = ClearRecoveredThresholdBias(CurrentBias, LocalPosition, SafeLimit, RetriggerBand);
    bool Triggered = false;

    for (size_t Axis = 0; Axis < 2; ++Axis) {
        const auto PositiveThreshold = static_cast<double>(SafeLimit + (NextBias[Axis] < 0 ? RetriggerBand : 0));
        const auto NegativeThreshold = -static_cast<double>(SafeLimit + (NextBias[Axis] > 0 ? RetriggerBand : 0));
        if (LocalPosition[Axis] > PositiveThreshold) {
            const auto WrapSteps = std::max(1, static_cast<int>(std::ceil((LocalPosition[Axis] - PositiveThreshold) / WrapWidth)));
            NextOffset[Axis] += WrapWidth * static_cast<double>(WrapSteps);
            NextBias[Axis] = 1;
            Triggered = true;
        } else if (LocalPosition[Axis] < NegativeThreshold) {
            const auto WrapSteps = std::max(1, static_cast<int>(std::ceil((NegativeThreshold - LocalPosition[Axis]) / WrapWidth)));
            NextOffset[Axis] -= WrapWidth * static_cast<double>(WrapSteps);
            NextBias[Axis] = -1;
            Triggered = true;
        }
    }

    NextBias[2] = 0;
    if (!Triggered) {
        return std::nullopt;
    }

    return AutomaticRebaseDecision {
        .Offset = NextOffset,
        .ThresholdBias = NextBias,
    };
}

bool SendAutomaticRebaseRequest(
    TClient& Client,
    const SpatialOffset& CurrentOffset,
    const SpatialOffset& LocalPosition,
    const AutomaticRebaseDecision& Decision,
    int SafeLimit,
    int RetriggerBand,
    int CooldownMs) {
    json Payload {
        { "offset", SerializeSpatialOffset(Decision.Offset) },
        { "reply_event_name", "BeamMPSpatialRebaseApplied" },
        { "request_id", fmt::format("auto-rebase:{}:{}", Client.GetID(), std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()) },
        { "meta", {
            { "automatic", true },
            { "safe_limit_meters", SafeLimit },
            { "wrap_width_meters", SafeLimit * 2 },
            { "retrigger_band_meters", RetriggerBand },
            { "cooldown_ms", CooldownMs },
        } },
    };

    const auto Packet = StringToVector("E:BeamMPSpatialRebase:" + Payload.dump());
    if (!LuaAPI::MP::Engine->Network().Respond(Client, Packet, true)) {
        beammp_errorf("Failed to send automatic spatial rebase request to client '{}' ({})", Client.GetName(), Client.GetID());
        LuaAPI::MP::Engine->Network().ClientKick(Client, "Disconnected after failing to receive automatic rebase packet");
        return false;
    }

    Application::Console().RecordEvent("spatial", "rebase_requested", {
        { "player", PlayerEventSnapshot(Client) },
        { "automatic", true },
        { "offset_before", SerializeSpatialOffset(CurrentOffset) },
        { "offset_after", SerializeSpatialOffset(Decision.Offset) },
        { "local_position", SerializeSpatialOffset(LocalPosition) },
        { "safe_limit_meters", SafeLimit },
        { "retrigger_band_meters", RetriggerBand },
        { "cooldown_ms", CooldownMs },
    });
    return true;
}

bool HandleSpatialAppliedClientEvent(TClient& Client, const std::string& Name, const std::string& Data) {
    if (Name != "BeamMPSpatialTeleportApplied" && Name != "BeamMPSpatialRebaseApplied") {
        return false;
    }

    auto Payload = json::parse(Data, nullptr, false);
    if (Payload.is_discarded() || !Payload.is_object()) {
        beammp_warnf("Client '{}' ({}) sent invalid spatial applied payload for '{}': {}", Client.GetName(), Client.GetID(), Name, Data);
        return true;
    }

    const auto MaybeOffset = ParseSpatialOffsetFromJson(Payload);
    if (!MaybeOffset.has_value()) {
        beammp_warnf("Client '{}' ({}) sent spatial applied event '{}' without a valid offset", Client.GetName(), Client.GetID(), Name);
        return true;
    }

    Client.SetSpatialOffset(*MaybeOffset);

    json EventPayload {
        { "player", PlayerEventSnapshot(Client) },
        { "offset", SerializeSpatialOffset(*MaybeOffset) },
        { "event_name", Name },
    };
    if (Payload.contains("request_id")) {
        EventPayload["request_id"] = Payload.at("request_id");
    }
    if (Payload.contains("reason")) {
        EventPayload["reason"] = Payload.at("reason");
    }
    if (Payload.contains("position")) {
        EventPayload["position"] = Payload.at("position");
    }
    if (Payload.contains("teleport")) {
        EventPayload["teleport"] = Payload.at("teleport");
    }
    if (Payload.contains("meta")) {
        EventPayload["meta"] = Payload.at("meta");
    }

    Application::Console().RecordEvent("spatial", Name == "BeamMPSpatialTeleportApplied" ? "teleport_applied" : "rebase_applied", std::move(EventPayload));
    return true;
}
}

static std::optional<std::pair<int, int>> GetPidVid(const std::string& str) {
    auto IDSep = str.find('-');
    std::string pid = str.substr(0, IDSep);
    std::string vid = str.substr(IDSep + 1);

    if (pid.find_first_not_of("0123456789") == std::string::npos && vid.find_first_not_of("0123456789") == std::string::npos) {
        try {
            int PID = stoi(pid);
            int VID = stoi(vid);
            return { { PID, VID } };
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}
TEST_CASE("GetPidVid") {
    SUBCASE("Valid singledigit") {
        const auto MaybePidVid = GetPidVid("0-1");
        CHECK(MaybePidVid);
        auto [pid, vid] = MaybePidVid.value();

        CHECK_EQ(pid, 0);
        CHECK_EQ(vid, 1);
    }
    SUBCASE("Valid doubledigit") {
        const auto MaybePidVid = GetPidVid("10-12");
        CHECK(MaybePidVid);
        auto [pid, vid] = MaybePidVid.value();

        CHECK_EQ(pid, 10);
        CHECK_EQ(vid, 12);
    }
    SUBCASE("Valid doubledigit 2") {
        const auto MaybePidVid = GetPidVid("10-2");
        CHECK(MaybePidVid);
        auto [pid, vid] = MaybePidVid.value();

        CHECK_EQ(pid, 10);
        CHECK_EQ(vid, 2);
    }
    SUBCASE("Valid doubledigit 3") {
        const auto MaybePidVid = GetPidVid("33-23");
        CHECK(MaybePidVid);
        auto [pid, vid] = MaybePidVid.value();

        CHECK_EQ(pid, 33);
        CHECK_EQ(vid, 23);
    }
    SUBCASE("Valid doubledigit 4") {
        const auto MaybePidVid = GetPidVid("3-23");
        CHECK(MaybePidVid);
        auto [pid, vid] = MaybePidVid.value();

        CHECK_EQ(pid, 3);
        CHECK_EQ(vid, 23);
    }
    SUBCASE("Empty string") {
        const auto MaybePidVid = GetPidVid("");
        CHECK(!MaybePidVid);
    }
    SUBCASE("Invalid separator") {
        const auto MaybePidVid = GetPidVid("0x0");
        CHECK(!MaybePidVid);
    }
    SUBCASE("Missing pid") {
        const auto MaybePidVid = GetPidVid("-0");
        CHECK(!MaybePidVid);
    }
    SUBCASE("Missing vid") {
        const auto MaybePidVid = GetPidVid("0-");
        CHECK(!MaybePidVid);
    }
    SUBCASE("Invalid pid") {
        const auto MaybePidVid = GetPidVid("x-0");
        CHECK(!MaybePidVid);
    }
    SUBCASE("Invalid vid") {
        const auto MaybePidVid = GetPidVid("0-x");
        CHECK(!MaybePidVid);
    }
}
TServer::TServer(const std::vector<std::string_view>& Arguments) {
    beammp_info("BeamMP Server v" + Application::ServerVersionString());
    Application::SetSubsystemStatus("Server", Application::Status::Starting);
    Application::SetSubsystemStatus("Server", Application::Status::Good);
}

void TServer::RemoveClient(const std::weak_ptr<TClient>& WeakClientPtr) {
    std::shared_ptr<TClient> LockedClientPtr { nullptr };
    try {
        LockedClientPtr = WeakClientPtr.lock();
    } catch (const std::exception&) {
        // silently fail, as there's nothing to do
        return;
    }
    beammp_assert(LockedClientPtr != nullptr);
    TClient& Client = *LockedClientPtr;
    beammp_debug("removing client " + Client.GetName() + " (" + std::to_string(ClientCount()) + ")");
    Client.ClearCars();
    WriteLock Lock(mClientsMutex);
    mClients.erase(WeakClientPtr.lock());
}

void TServer::ForEachClient(const std::function<bool(std::weak_ptr<TClient>)>& Fn) {
    decltype(mClients) Clients;
    {
        ReadLock lock(mClientsMutex);
        Clients = mClients;
    }
    for (auto& Client : Clients) {
        if (!Fn(Client)) {
            break;
        }
    }
}

size_t TServer::ClientCount() const {
    ReadLock Lock(mClientsMutex);
    return mClients.size();
}

void TServer::GlobalParser(const std::weak_ptr<TClient>& Client, std::vector<uint8_t>&& Packet, TPPSMonitor& PPSMonitor, TNetwork& Network, bool udp) {
    constexpr std::string_view ABG = "ABG:";
    if (Packet.size() >= ABG.size() && std::equal(Packet.begin(), Packet.begin() + ABG.size(), ABG.begin(), ABG.end())) {
        Packet.erase(Packet.begin(), Packet.begin() + ABG.size());
        try {
            Packet = DeComp(Packet);
        } catch (const InvalidDataError& ) {
            auto LockedClient = Client.lock();
            beammp_errorf("Failed to decompress packet from client {}. The client sent invalid data and will now be disconnected.", LockedClient->GetID());
            Network.ClientKick(*LockedClient, "Sent invalid compressed packet (this is likely a bug on your end)");
            return;
        } catch (const std::runtime_error& e) {
            auto LockedClient = Client.lock();
            beammp_errorf("Failed to decompress packet from client {}: {}. The server might be out of RAM! The client will now be disconnected.", LockedClient->GetID(), e.what());
            Network.ClientKick(*LockedClient, "Decompression failed (likely a server-side problem)");
            return;
        }
    }
    if (Packet.empty()) {
        return;
    }

    if (Client.expired()) {
        return;
    }
    auto LockedClient = Client.lock();

    std::any Res;
    char Code = Packet.at(0);

    std::string StringPacket(reinterpret_cast<const char*>(Packet.data()), Packet.size());

    // V to Y
    if (Code <= 89 && Code >= 86) {
        int PID = -1;
        int VID = -1;

        auto pidVidPart = StringPacket.substr(3);
        auto MaybePidVid = GetPidVid(pidVidPart.substr(0, pidVidPart.find(':')));
        if (MaybePidVid) {
            std::tie(PID, VID) = MaybePidVid.value();
        }

        if (PID == -1 || VID == -1 || PID != LockedClient->GetID()) {
            return;
        }

        PPSMonitor.IncrementInternalPPS();
        Network.SendToAll(LockedClient.get(), Packet, false, false);
        return;
    }
    switch (Code) {
    case 'H': // initial connection
        if (udp) {
            beammp_debugf("Received 'H' packet over UDP from client '{}' ({}), ignoring it", LockedClient->GetName(), LockedClient->GetID());
            return;
        }
        if (!Network.SyncClient(Client)) {
            // TODO handle
        }
        return;
    case 'p':
        if (!Network.Respond(*LockedClient, StringToVector("p"), false)) {
            // failed to send
            LockedClient->Disconnect("Failed to send ping");
        } else {
            Network.UpdatePlayer(*LockedClient);
        }
        return;
    case 'O':
        if (udp) {
            beammp_debugf("Received 'O' packet over UDP from client '{}' ({}), ignoring it", LockedClient->GetName(), LockedClient->GetID());
            return;
        }
        if (Packet.size() > 1000) {
            beammp_debug(("Received data from: ") + LockedClient->GetName() + (" Size: ") + std::to_string(Packet.size()));
        }
        ParseVehicle(*LockedClient, StringPacket, Network);
        return;
    case 'C': {
        if (udp) {
            beammp_debugf("Received 'C' packet over UDP from client '{}' ({}), ignoring it", LockedClient->GetName(), LockedClient->GetID());
            return;
        }
        if (Packet.size() < 4 || std::find(Packet.begin() + 3, Packet.end(), ':') == Packet.end())
            break;
        const auto PacketAsString = std::string(reinterpret_cast<const char*>(Packet.data()), Packet.size());
        std::string Message = "";
        const auto ColonPos = PacketAsString.find(':', 3);
        if (ColonPos != std::string::npos && ColonPos + 2 < PacketAsString.size()) {
            Message = PacketAsString.substr(ColonPos + 2);
        }
        if (Message.empty()) {
            beammp_debugf("Empty chat message received from '{}' ({}), ignoring it", LockedClient->GetName(), LockedClient->GetID());
            return;
        }
        if (Message.size() > 500) {
           beammp_debugf("Chat message too long from '{}' ({}), ignoring it", LockedClient->GetName(), LockedClient->GetID());
           return;
        }
        auto Futures = LuaAPI::MP::Engine->TriggerEvent("onChatMessage", "", LockedClient->GetID(), LockedClient->GetName(), Message);
        TLuaEngine::WaitForAll(Futures);
        LogChatMessage(LockedClient->GetName(), LockedClient->GetID(), PacketAsString.substr(PacketAsString.find(':', 3) + 1));
        Application::Console().RecordEvent("chat", "player_message", {
            { "player_id", LockedClient->GetID() },
            { "player_name", LockedClient->GetName() },
            { "message", Message },
        });
        bool Rejected = std::any_of(Futures.begin(), Futures.end(),
            [](const std::shared_ptr<TLuaResult>& Elem) {
                return !Elem->Error
                    && Elem->Result.is<int>()
                    && bool(Elem->Result.as<int>());
            });
        if (!Rejected) {
            std::string SanitizedPacket = fmt::format("C:{}: {}", LockedClient->GetName(), Message);
            Network.SendToAll(nullptr, StringToVector(SanitizedPacket), true, true);
        }
        auto PostFutures = LuaAPI::MP::Engine->TriggerEvent("postChatMessage", "", !Rejected, LockedClient->GetID(), LockedClient->GetName(), Message);
        LuaAPI::MP::Engine->ReportErrors(PostFutures);
        return;
    }
    case 'E':
        if (udp) {
            beammp_debugf("Received 'E' packet over UDP from client '{}' ({}), ignoring it", LockedClient->GetName(), LockedClient->GetID());
            return;
        }
        HandleEvent(*LockedClient, StringPacket);
        return;
    case 'N':
        Network.SendToAll(LockedClient.get(), Packet, false, true);
        return;
    case 'Z': { // position packet
        PPSMonitor.IncrementInternalPPS();

        int PID = -1;
        int VID = -1;

        auto pidVidPart = StringPacket.substr(3);
        auto MaybePidVid = GetPidVid(pidVidPart.substr(0, pidVidPart.find(':')));
        if (MaybePidVid) {
            std::tie(PID, VID) = MaybePidVid.value();
        }

        if (PID == -1 || VID == -1 || PID != LockedClient->GetID()) {
            return;
        }

        Network.SendToAll(LockedClient.get(), Packet, false, false);
        HandlePosition(*LockedClient, StringPacket);
        return;
    }
    default:
        return;
    }
}

void TServer::HandleEvent(TClient& c, const std::string& RawData) {
    // E:Name:Data
    // Data is allowed to have ':'
    if (RawData.size() < 2) {
        beammp_debugf("Client '{}' ({}) tried to send an empty event, ignoring", c.GetName(), c.GetID());
        return;
    }
    auto NameDataSep = RawData.find(':', 2);
    if (NameDataSep == std::string::npos) {
        beammp_warn("received event in invalid format (missing ':'), got: '" + RawData + "'");
    }
    std::string Name = RawData.substr(2, NameDataSep - 2);
    std::string Data = RawData.substr(NameDataSep + 1);

    if (HandleSpatialAppliedClientEvent(c, Name, Data)) {
        return;
    }

    std::vector<std::string> exclude = {"onInit", "onFileChanged","onVehicleDeleted","onConsoleInput","onPlayerAuth","postPlayerAuth", "onPlayerDisconnect",
    "onPlayerConnecting","onPlayerJoining","onPlayerJoin","onChatMessage","postChatMessage","onVehicleSpawn","postVehicleSpawn","onVehicleEdited", "postVehicleEdited",
    "onVehicleReset","onVehiclePaintChanged","onShutdown"};

    if (std::ranges::find(exclude, Name) != exclude.end()) {
        beammp_debugf("Excluded event triggered by client '{}' ({}): '{}', ignoring.", c.GetName(), c.GetID(), Name);
        return;
    }
    Application::Console().RecordEvent("client", "event", {
        { "player_id", c.GetID() },
        { "player_name", c.GetName() },
        { "event_name", Name },
        { "data", Data },
    });
    LuaAPI::MP::Engine->ReportErrors(LuaAPI::MP::Engine->TriggerEvent(Name, "", c.GetID(), Data));
}

bool TServer::IsUnicycle(TClient& c, const std::string& CarJson) {
    try {
        auto Car = nlohmann::json::parse(CarJson);
        const std::string jbm = "jbm";
        if (Car.contains(jbm) && Car[jbm].is_string() && Car[jbm] == "unicycle") {
            return true;
        }
    } catch (const std::exception& e) {
        beammp_warn("Failed to parse vehicle data as json for client " + std::to_string(c.GetID()) + ": '" + CarJson + "'.");
    }
    return false;
}

bool TServer::ShouldSpawn(TClient& c, const std::string& CarJson, int ID) {
    if (IsUnicycle(c, CarJson) && c.GetUnicycleID() < 0) {
        c.SetUnicycleID(ID);
        return true;
    } else {
        return c.GetCarCount() < Application::Settings.getAsInt(Settings::Key::General_MaxCars);
    }
}

void TServer::ParseVehicle(TClient& c, const std::string& Pckt, TNetwork& Network) {
    if (Pckt.length() < 6)
        return;
    std::string Packet = Pckt;
    char Code = Packet.at(1);
    int PID = -1;
    int VID = -1;
    std::string Data = Packet.substr(3), pid, vid;
    switch (Code) { // Spawned Destroyed Switched/Moved NotFound Reset
    case 's':
        beammp_tracef("got 'Os' packet: '{}' ({})", Packet, Packet.size());
        if (Data.at(0) == '0') {
            int CarID = c.GetOpenCarID();
            beammp_debugf("'{}' created a car with ID {}", c.GetName(), CarID);

            std::string CarJson = Packet.substr(5);
            Packet = "Os:" + c.GetRoles() + ":" + c.GetName() + ":" + std::to_string(c.GetID()) + "-" + std::to_string(CarID) + ":" + CarJson;
            auto Futures = LuaAPI::MP::Engine->TriggerEvent("onVehicleSpawn", "", c.GetID(), CarID, Packet.substr(3));
            TLuaEngine::WaitForAll(Futures);
            bool ShouldntSpawn = std::any_of(Futures.begin(), Futures.end(),
                [](const std::shared_ptr<TLuaResult>& Result) {
                    return !Result->Error && Result->Result.is<int>() && Result->Result.as<int>() != 0;
                });

            bool SpawnConfirmed = false;
            auto CarJsonDoc = nlohmann::json::parse(CarJson, nullptr, false);
            if (ShouldSpawn(c, CarJson, CarID) && !ShouldntSpawn && !CarJsonDoc.is_discarded()) {
                c.AddNewCar(CarID, CarJsonDoc);
                Network.SendToAll(nullptr, StringToVector(Packet), true, true);
                SpawnConfirmed = true;
            } else {
                if (!Network.Respond(c, StringToVector(Packet), true)) {
                    // TODO: handle
                }
                std::string Destroy = "Od:" + std::to_string(c.GetID()) + "-" + std::to_string(CarID);
                LuaAPI::MP::Engine->ReportErrors(LuaAPI::MP::Engine->TriggerEvent("onVehicleDeleted", "", c.GetID(), CarID));
                if (!Network.Respond(c, StringToVector(Destroy), true)) {
                    // TODO: handle
                }
                beammp_debugf("{} (force : car limit/lua) removed ID {}", c.GetName(), CarID);
                SpawnConfirmed = false;
            }
            auto PostFutures = LuaAPI::MP::Engine->TriggerEvent("postVehicleSpawn", "", SpawnConfirmed, c.GetID(), CarID, Packet.substr(3));
            // the post event is not cancellable so we dont wait for it
            LuaAPI::MP::Engine->ReportErrors(PostFutures);
        }
        return;
    case 'c': {
        beammp_trace(std::string(("got 'Oc' packet: '")) + Packet + ("' (") + std::to_string(Packet.size()) + (")"));
        auto MaybePidVid = GetPidVid(Data.substr(0, Data.find(':', 1)));
        if (MaybePidVid) {
            std::tie(PID, VID) = MaybePidVid.value();
        }
        if (PID != -1 && VID != -1 && PID == c.GetID()) {
            auto Futures = LuaAPI::MP::Engine->TriggerEvent("onVehicleEdited", "", c.GetID(), VID, Packet.substr(3));
            TLuaEngine::WaitForAll(Futures);
            bool ShouldntAllow = std::any_of(Futures.begin(), Futures.end(),
                [](const std::shared_ptr<TLuaResult>& Result) {
                    return !Result->Error && Result->Result.is<int>() && Result->Result.as<int>() != 0;
                });

            auto FoundPos = Packet.find('{');
            FoundPos = FoundPos == std::string::npos ? 0 : FoundPos; // attempt at sanitizing this
            bool Allowed = false;
            if ((c.GetUnicycleID() != VID || IsUnicycle(c, Packet.substr(FoundPos)))
                && !ShouldntAllow) {
                Network.SendToAll(&c, StringToVector(Packet), false, true);
                Apply(c, VID, Packet);
                Allowed = true;
            } else {
                if (c.GetUnicycleID() == VID) {
                    c.SetUnicycleID(-1);
                }
                std::string Destroy = "Od:" + std::to_string(c.GetID()) + "-" + std::to_string(VID);
                Network.SendToAll(nullptr, StringToVector(Destroy), true, true);
                LuaAPI::MP::Engine->ReportErrors(LuaAPI::MP::Engine->TriggerEvent("onVehicleDeleted", "", c.GetID(), VID));
                c.DeleteCar(VID);
                Allowed = false;
            }

            auto PostFutures = LuaAPI::MP::Engine->TriggerEvent("postVehicleEdited", "", Allowed, c.GetID(), VID, Packet.substr(3));
            // the post event is not cancellable so we dont wait for it
            LuaAPI::MP::Engine->ReportErrors(PostFutures);
        }
        return;
    }
    case 'd': {
        beammp_trace(std::string(("got 'Od' packet: '")) + Packet + ("' (") + std::to_string(Packet.size()) + (")"));
        auto MaybePidVid = GetPidVid(Data.substr(0, Data.find(':', 1)));
        if (MaybePidVid) {
            std::tie(PID, VID) = MaybePidVid.value();
        }
        if (PID != -1 && VID != -1 && PID == c.GetID()) {
            if (c.GetUnicycleID() == VID) {
                c.SetUnicycleID(-1);
            }
            Network.SendToAll(nullptr, StringToVector(Packet), true, true);
            // TODO: should this trigger on all vehicle deletions?
            LuaAPI::MP::Engine->ReportErrors(LuaAPI::MP::Engine->TriggerEvent("onVehicleDeleted", "", c.GetID(), VID));
            c.DeleteCar(VID);
            beammp_debug(c.GetName() + (" deleted car with ID ") + std::to_string(VID));
        }
        return;
    }
    case 'r': {
        beammp_trace(std::string(("got 'Or' packet: '")) + Packet + ("' (") + std::to_string(Packet.size()) + (")"));
        auto MaybePidVid = GetPidVid(Data.substr(0, Data.find(':', 1)));
        if (MaybePidVid) {
            std::tie(PID, VID) = MaybePidVid.value();
        }

        if (PID != -1 && VID != -1 && PID == c.GetID()) {
            auto BracketPos = Data.find('{');
            if (BracketPos == std::string::npos) {
                beammp_debugf("Invalid 'Or' packet body from client {}", c.GetID());
                return;
            }

            Data = Data.substr(BracketPos);
            LuaAPI::MP::Engine->ReportErrors(LuaAPI::MP::Engine->TriggerEvent("onVehicleReset", "", c.GetID(), VID, Data));
            Network.SendToAll(&c, StringToVector(Packet), false, true);
        }
        return;
    }
    case 't': {
        beammp_trace(std::string(("got 'Ot' packet: '")) + Packet + ("' (") + std::to_string(Packet.size()) + (")"));
        auto MaybePidVid = GetPidVid(Data.substr(0, Data.find(':', 1)));
        if (MaybePidVid) {
            std::tie(PID, VID) = MaybePidVid.value();
        }
        if (PID != -1 && VID != -1 && PID == c.GetID()) {
            Network.SendToAll(&c, StringToVector(Packet), false, true);
        }
        return;
    }
    case 'm': {
        Network.SendToAll(&c, StringToVector(Packet), false, true);
        return;
    }
    case 'p': {
        beammp_trace(std::string(("got 'Op' packet: '")) + Packet + ("' (") + std::to_string(Packet.size()) + (")"));
        auto MaybePidVid = GetPidVid(Data.substr(0, Data.find(':', 1)));
        if (MaybePidVid) {
            std::tie(PID, VID) = MaybePidVid.value();
        }

        if (PID != -1 && VID != -1 && PID == c.GetID()) {
            auto BracketPos = Data.find('[');
            if (BracketPos == std::string::npos) {
                beammp_debugf("Invalid 'Op' packet body from client {}", c.GetID());
                return;
            }

            Data = Data.substr(BracketPos);

            LuaAPI::MP::Engine->ReportErrors(LuaAPI::MP::Engine->TriggerEvent("onVehiclePaintChanged", "", c.GetID(), VID, Data));
            Network.SendToAll(&c, StringToVector(Packet), false, true);

            auto CarData = c.GetCarData(VID);
            if (CarData == nlohmann::detail::value_t::null)
                return;

            if (CarData.contains("vcf") && CarData.at("vcf").is_object())
                if (CarData.at("vcf").contains("paints") && CarData.at("vcf").at("paints").is_array()) {
                    CarData.at("vcf")["paints"] = nlohmann::json::parse(Data);
                    c.SetCarData(VID, CarData);
                }

        }
        return;
    }
    default:
        beammp_trace(std::string(("possibly not implemented: '") + Packet + ("' (") + std::to_string(Packet.size()) + (")")));
        return;
    }
}

void TServer::Apply(TClient& c, int VID, const std::string& pckt) {
    auto FoundPos = pckt.find('{');
    if (FoundPos == std::string::npos) {
        beammp_error("Malformed packet received, no '{' found");
        return;
    }

    std::string Packet = pckt.substr(FoundPos);
    nlohmann::json VD = c.GetCarData(VID);
    if (VD == nlohmann::detail::value_t::null) {
        beammp_error("Tried to apply change to vehicle that does not exist");
        return;
    }

    nlohmann::json Pack = nlohmann::json::parse(Packet, nullptr, false);

    if (Pack.is_discarded()) {
        beammp_error("Could not get active vehicle config!");
        return;
    }

    c.SetCarData(VID, Pack);
}

void TServer::InsertClient(const std::shared_ptr<TClient>& NewClient) {
    beammp_debug("inserting client (" + std::to_string(ClientCount()) + ")");
    WriteLock Lock(mClientsMutex); // TODO why is there 30+ threads locked here
    (void)mClients.insert(NewClient);
}

struct PidVidData {
    int PID;
    int VID;
    std::string Data;
};

static std::optional<PidVidData> ParsePositionPacket(const std::string& Packet) {
    if (Packet.size() < 3) {
        // invalid packet
        return std::nullopt;
    }
    // Zp:PID-VID:DATA
    std::string withoutCode = Packet.substr(3);

    // parse veh ID
    if (auto DataBeginPos = withoutCode.find('{'); DataBeginPos != std::string::npos && DataBeginPos != 0) {
        // separator is :{, so position of { minus one
        auto PidVidOnly = withoutCode.substr(0, DataBeginPos - 1);
        auto MaybePidVid = GetPidVid(PidVidOnly);
        if (MaybePidVid) {
            int PID = -1;
            int VID = -1;
            // FIXME: check that the VID and PID are valid, so that we don't waste memory
            std::tie(PID, VID) = MaybePidVid.value();

            std::string Data = withoutCode.substr(DataBeginPos);
            return PidVidData {
                .PID = PID,
                .VID = VID,
                .Data = Data,
            };
        } else {
            // invalid packet
            return std::nullopt;
        }
    }
    // invalid packet
    return std::nullopt;
}

TEST_CASE("ParsePositionPacket") {
    const auto TestData = R"({"tim":10.428000331623,"vel":[-2.4171722121385e-05,-9.7184734153252e-06,-7.6420763232237e-06],"rot":[-0.0001296154171915,0.0031575385950029,0.98994906610295,0.14138903660382],"rvel":[5.3640324636461e-05,-9.9824529946024e-05,5.1664064641372e-05],"pos":[-0.27281248907838,-0.20515357944633,0.49695488960431],"ping":0.032999999821186})";
    SUBCASE("All the pids and vids") {
        for (int pid = 0; pid < 100; ++pid) {
            for (int vid = 0; vid < 100; ++vid) {
                std::optional<PidVidData> MaybeRes = ParsePositionPacket(fmt::format("Zp:{}-{}:{}", pid, vid, TestData));
                CHECK(MaybeRes.has_value());
                CHECK_EQ(MaybeRes.value().PID, pid);
                CHECK_EQ(MaybeRes.value().VID, vid);
                CHECK_EQ(MaybeRes.value().Data, TestData);
            }
        }
    }
}

void TServer::HandlePosition(TClient& c, const std::string& Packet) {
    if (auto Parsed = ParsePositionPacket(Packet); Parsed.has_value()) {
        c.SetCarPosition(Parsed.value().VID, Parsed.value().Data);

        const auto MaybeLocalPosition = ParseLocalPositionFromRawPacket(Parsed.value().Data);
        if (!MaybeLocalPosition.has_value()) {
            return;
        }

        const auto SafeLimit = Application::Settings.getAsInt(Settings::Key::SpatialRebase_AutoSafeLimitMeters);
        if (SafeLimit <= 0) {
            return;
        }

        const auto RetriggerBand = std::max(0, Application::Settings.getAsInt(Settings::Key::SpatialRebase_AutoRetriggerBandMeters));
        const auto CooldownMs = std::max(0, Application::Settings.getAsInt(Settings::Key::SpatialRebase_AutoCooldownMs));
        const auto CurrentOffset = c.GetSpatialOffset();
        const auto CurrentBias = c.GetAutoRebaseThresholdBias();
        const auto ClearedBias = ClearRecoveredThresholdBias(CurrentBias, *MaybeLocalPosition, SafeLimit, RetriggerBand);
        if (ClearedBias != CurrentBias) {
            c.SetAutoRebaseThresholdBias(ClearedBias);
        }

        const auto MaybeDecision = EvaluateAutomaticRebase(CurrentOffset, *MaybeLocalPosition, ClearedBias, SafeLimit, RetriggerBand);
        if (!MaybeDecision.has_value()) {
            return;
        }

        if (!c.TryBeginPendingSpatialRebase(MaybeDecision->Offset, MaybeDecision->ThresholdBias, std::chrono::milliseconds(CooldownMs))) {
            return;
        }

        (void)SendAutomaticRebaseRequest(c, CurrentOffset, *MaybeLocalPosition, *MaybeDecision, SafeLimit, RetriggerBand, CooldownMs);
    }
}
