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

#include <array>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <optional>
#include <queue>
#include <string>
#include <unordered_set>
#include <utility>

#include "BoostAliases.h"
#include "Common.h"
#include "Compat.h"
#include "VehicleData.h"

class TServer;

#ifdef BEAMMP_WINDOWS
// for socklen_t
#include <WS2tcpip.h>
#endif // WINDOWS

struct TConnection final {
    ip::tcp::socket Socket;
    ip::tcp::endpoint SockAddr;
};

class TClient final {
public:
    using TSetOfVehicleData = std::vector<TVehicleData>;
    using TSpatialOffset = std::array<double, 3>;
    using TAutoRebaseThresholdBias = std::array<int, 3>;

    struct TVehicleDataLockPair {
        TSetOfVehicleData* VehicleData;
        std::unique_lock<std::mutex> Lock;
    };

    TClient(TServer& Server, ip::tcp::socket&& Socket);
    TClient(const TClient&) = delete;
    ~TClient();
    TClient& operator=(const TClient&) = delete;

    void AddNewCar(int Ident, const nlohmann::json& Data);
    void SetCarData(int Ident, const nlohmann::json& Data);
    void SetCarPosition(int Ident, const std::string& Data);
    TVehicleDataLockPair GetAllCars();
    void SetName(const std::string& Name) { mName = Name; }
    void SetRoles(const std::string& Role) { mRole = Role; }
    void SetIdentifier(const std::string& key, const std::string& value) { mIdentifiers[key] = value; }
    nlohmann::json GetCarData(int Ident);
    std::string GetCarPositionRaw(int Ident);
    void SetSpatialOffset(const TSpatialOffset& Offset);
    [[nodiscard]] TSpatialOffset GetSpatialOffset() const;
    [[nodiscard]] bool TryBeginPendingSpatialRebase(const TSpatialOffset& Offset, const TAutoRebaseThresholdBias& ThresholdBias, std::chrono::milliseconds Cooldown);
    [[nodiscard]] bool HasPendingSpatialRebase() const;
    [[nodiscard]] bool IsAutomaticSpatialRebaseCoolingDown(std::chrono::milliseconds Cooldown) const;
    void ClearPendingSpatialRebase();
    [[nodiscard]] TAutoRebaseThresholdBias GetAutoRebaseThresholdBias() const;
    void SetAutoRebaseThresholdBias(const TAutoRebaseThresholdBias& ThresholdBias);
    void SetUDPAddr(const ip::udp::endpoint& Addr) { mUDPAddress = Addr; }
    void SetTCPSock(ip::tcp::socket&& CSock) { mSocket = std::move(CSock); }
    void Disconnect(std::string_view Reason);
    bool IsDisconnected() const { return mDisconnectRequested || !mSocket.is_open(); }
    // locks
    void DeleteCar(int Ident);
    [[nodiscard]] const std::unordered_map<std::string, std::string>& GetIdentifiers() const { return mIdentifiers; }
    [[nodiscard]] const ip::udp::endpoint& GetUDPAddr() const { return mUDPAddress; }
    [[nodiscard]] ip::udp::endpoint& GetUDPAddr() { return mUDPAddress; }
    [[nodiscard]] ip::tcp::socket& GetTCPSock() { return mSocket; }
    [[nodiscard]] const ip::tcp::socket& GetTCPSock() const { return mSocket; }
    [[nodiscard]] std::string GetRoles() const { return mRole; }
    [[nodiscard]] std::string GetName() const { return mName; }
    void SetUnicycleID(int ID) { mUnicycleID = ID; }
    void SetID(int ID) { mID = ID; }
    [[nodiscard]] int GetOpenCarID() const;
    [[nodiscard]] int GetCarCount() const;
    void ClearCars();
    [[nodiscard]] int GetID() const { return mID; }
    [[nodiscard]] int GetUnicycleID() const { return mUnicycleID; }
    [[nodiscard]] bool IsUDPConnected() const { return mIsUDPConnected; }
    [[nodiscard]] bool IsSynced() const { return mIsSynced; }
    [[nodiscard]] bool IsSyncing() const { return mIsSyncing; }
    [[nodiscard]] bool IsGuest() const { return mIsGuest; }
    void SetIsGuest(bool NewIsGuest) { mIsGuest = NewIsGuest; }
    void SetIsSynced(bool NewIsSynced) { mIsSynced = NewIsSynced; }
    void SetIsSyncing(bool NewIsSyncing) { mIsSyncing = NewIsSyncing; }
    void EnqueuePacket(const std::vector<uint8_t>& Packet);
    [[nodiscard]] std::queue<std::vector<uint8_t>>& MissedPacketQueue() { return mPacketsSync; }
    [[nodiscard]] const std::queue<std::vector<uint8_t>>& MissedPacketQueue() const { return mPacketsSync; }
    [[nodiscard]] size_t MissedPacketQueueSize() const { return mPacketsSync.size(); }
    [[nodiscard]] std::mutex& MissedPacketQueueMutex() const { return mMissedPacketsMutex; }
    [[nodiscard]] std::mutex& SocketMutex() const { return mSocketMutex; }
    void EnqueueTCPWrite(std::vector<uint8_t>&& Packet);
    bool WaitForNextTCPWrite(std::vector<uint8_t>& Packet);
    void ClearPendingTCPWrites();
    void RequestDisconnect(std::string_view Reason);
    [[nodiscard]] bool IsDisconnectRequested() const { return mDisconnectRequested; }
    [[nodiscard]] bool HasTCPWriter() const { return mTCPWriterActive; }
    void SetTCPWriterActive(bool Active) { mTCPWriterActive = Active; }
    [[nodiscard]] std::string DisconnectReason() const;
    void SetIsUDPConnected(bool NewIsConnected) { mIsUDPConnected = NewIsConnected; }
    [[nodiscard]] TServer& Server() const;
    void UpdatePingTime();
    int SecondsSinceLastPing();
    void SetMagic(std::vector<uint8_t> magic) { mMagic = std::move(magic); }
    [[nodiscard]] const std::vector<uint8_t>& GetMagic() const { return mMagic; }

private:
    void InsertVehicle(int ID, const std::string& Data);

    TServer& mServer;
    bool mIsUDPConnected = false;
    bool mIsSynced = false;
    bool mIsSyncing = false;
    bool mDisconnectRequested = false;
    bool mTCPWriterActive = false;
    mutable std::mutex mMissedPacketsMutex;
    mutable std::mutex mSocketMutex;
    mutable std::mutex mTCPWriteMutex;
    mutable std::mutex mDisconnectMutex;
    std::condition_variable mTCPWriteCV;
    std::queue<std::vector<uint8_t>> mPacketsSync;
    std::queue<std::vector<uint8_t>> mPendingTCPWrites;
    std::unordered_map<std::string, std::string> mIdentifiers;
    bool mIsGuest = false;
    mutable std::mutex mVehicleDataMutex;
    mutable std::mutex mVehiclePositionMutex;
    mutable std::mutex mSpatialOffsetMutex;
    TSetOfVehicleData mVehicleData;
    SparseArray<std::string> mVehiclePosition;
    TSpatialOffset mSpatialOffset { 0.0, 0.0, 0.0 };
    std::optional<TSpatialOffset> mPendingSpatialRebase;
    TAutoRebaseThresholdBias mAutoRebaseThresholdBias { 0, 0, 0 };
    std::chrono::steady_clock::time_point mLastAutomaticSpatialRebaseAt {};
    std::string mName = "Unknown Client";
    ip::tcp::socket mSocket;
    ip::udp::endpoint mUDPAddress {};
    int mUnicycleID = -1;
    std::string mRole;
    std::string mDID;
    int mID = -1;
    std::chrono::time_point<std::chrono::high_resolution_clock> mLastPingTime = std::chrono::high_resolution_clock::now();
    std::string mDisconnectReason;
    std::vector<uint8_t> mMagic;
};

std::optional<std::weak_ptr<TClient>> GetClient(class TServer& Server, int ID);
