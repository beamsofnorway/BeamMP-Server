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

#include "Settings.h"

#include <charconv>

Settings::Settings() {
    SettingsMap = std::unordered_map<Key, SettingsTypeVariant> {
        // All entries which contain std::strings must be explicitly constructed, otherwise they become 'bool'
        { General_Description, std::string("BeamMP Default Description") },
        { General_Tags, std::string("Freeroam") },
        { General_MaxPlayers, 8 },
        { General_Name, std::string("BeamMP Server") },
        { General_Map, std::string("/levels/gridmap_v2/info.json") },
        { General_AuthKey, std::string("") },
        { General_Private, true },
        { General_IP, "::"},
        { General_Port, 30814 },
        { General_MaxCars, 1 },
        { General_LogChat, true },
        { General_ResourceFolder, std::string("Resources") },
        { General_Debug, false },
        { General_AllowGuests, true },
        { General_InformationPacket, true },
        { HttpApi_Enabled, true },
        { HttpApi_Host, std::string("127.0.0.1") },
        { HttpApi_Port, 30815 },
        { HttpApi_Token, std::string("") },
        { SpatialRebase_AutoSafeLimitMeters, 48 },
        { SpatialRebase_AutoRetriggerBandMeters, 8 },
        { SpatialRebase_AutoCooldownMs, 1000 },
        { Misc_ImScaredOfUpdates, true },
        { Misc_UpdateReminderTime, "30s" }
    };

    InputAccessMapping = std::unordered_map<ComposedKey, SettingsAccessControl> {
        { { "General", "Description" }, { General_Description, READ_WRITE } },
        { { "General", "Tags" }, { General_Tags, READ_WRITE } },
        { { "General", "MaxPlayers" }, { General_MaxPlayers, READ_WRITE } },
        { { "General", "Name" }, { General_Name, READ_WRITE } },
        { { "General", "Map" }, { General_Map, READ_WRITE } },
        { { "General", "AuthKey" }, { General_AuthKey, NO_ACCESS } },
        { { "General", "Private" }, { General_Private, READ_ONLY } },
        { { "General", "IP" }, { General_IP, READ_ONLY } },
        { { "General", "Port" }, { General_Port, READ_ONLY } },
        { { "General", "MaxCars" }, { General_MaxCars, READ_WRITE } },
        { { "General", "LogChat" }, { General_LogChat, READ_ONLY } },
        { { "General", "ResourceFolder" }, { General_ResourceFolder, READ_ONLY } },
        { { "General", "Debug" }, { General_Debug, READ_WRITE } },
        { { "General", "AllowGuests" }, { General_AllowGuests, READ_WRITE } },
        { { "General", "InformationPacket" }, { General_InformationPacket, READ_WRITE } },
        { { "HttpApi", "Enabled" }, { HttpApi_Enabled, READ_ONLY } },
        { { "HttpApi", "Host" }, { HttpApi_Host, READ_ONLY } },
        { { "HttpApi", "Port" }, { HttpApi_Port, READ_ONLY } },
        { { "HttpApi", "Token" }, { HttpApi_Token, NO_ACCESS } },
        { { "SpatialRebase", "AutoSafeLimitMeters" }, { SpatialRebase_AutoSafeLimitMeters, READ_WRITE } },
        { { "SpatialRebase", "AutoRetriggerBandMeters" }, { SpatialRebase_AutoRetriggerBandMeters, READ_WRITE } },
        { { "SpatialRebase", "AutoCooldownMs" }, { SpatialRebase_AutoCooldownMs, READ_WRITE } },
        { { "Misc", "ImScaredOfUpdates" }, { Misc_ImScaredOfUpdates, READ_WRITE } },
        { { "Misc", "UpdateReminderTime" }, { Misc_UpdateReminderTime, READ_WRITE } }
    };
}

std::string Settings::getAsString(Key key) {
    auto map = SettingsMap.synchronize();
    if (!map->contains(key)) {
        throw std::logic_error { "Undefined key accessed in Settings::getAsString" };
    }
    return std::get<std::string>(map->at(key));
}
int Settings::getAsInt(Key key) {
    auto map = SettingsMap.synchronize();
    if (!map->contains(key)) {
        throw std::logic_error { "Undefined key accessed in Settings::getAsInt" };
    }
    return std::get<int>(map->at(key));
}

bool Settings::getAsBool(Key key) {
    auto map = SettingsMap.synchronize();
    if (!map->contains(key)) {
        throw std::logic_error { "Undefined key accessed in Settings::getAsBool" };
    }
    return std::get<bool>(map->at(key));
}

Settings::SettingsTypeVariant Settings::get(Key key) {
    auto map = SettingsMap.synchronize();
    if (!map->contains(key)) {
        throw std::logic_error { "Undefined setting key accessed in Settings::get" };
    }
    return map->at(key);
}

void Settings::set(Key key, const std::string& value) {
    auto map = SettingsMap.synchronize();
    if (!map->contains(key)) {
        throw std::logic_error { "Undefined setting key accessed in Settings::set(std::string)" };
    }
    if (!std::holds_alternative<std::string>(map->at(key))) {
        throw std::logic_error { fmt::format("Wrong value type in Settings::set(std::string): index {}", map->at(key).index()) };
    }
    map->at(key) = value;
}

const std::unordered_map<ComposedKey, Settings::SettingsAccessControl> Settings::getAccessControlMap() const {
    return *InputAccessMapping;
}

Settings::SettingsAccessControl Settings::getConsoleInputAccessMapping(const ComposedKey& keyName) {
    auto acl_map = InputAccessMapping.synchronize();
    if (!acl_map->contains(keyName)) {
        throw std::logic_error { "Unknown key name accessed in Settings::getConsoleInputAccessMapping" };
    } else if (acl_map->at(keyName).second == SettingsAccessMask::NO_ACCESS) {
        throw std::logic_error { "Setting '" + keyName.Category + "::" + keyName.Key + "' is not accessible from within the runtime!" };
    }
    return acl_map->at(keyName);
}

void Settings::setConsoleInputAccessMapping(const ComposedKey& keyName, const std::string& value) {
    auto [map, acl_map] = boost::synchronize(SettingsMap, InputAccessMapping);
    if (!acl_map->contains(keyName)) {
        throw std::logic_error { "Unknown key name accessed in Settings::setConsoleInputAccessMapping" };
    } else if (acl_map->at(keyName).second == SettingsAccessMask::NO_ACCESS) {
        throw std::logic_error { "Setting '" + keyName.Category + "::" + keyName.Key + "' is not accessible from within the runtime!" };
    } else if (acl_map->at(keyName).second == SettingsAccessMask::READ_ONLY) {
        throw std::logic_error { "Setting '" + keyName.Category + "::" + keyName.Key + "' is not writeable from within the runtime!" };
    }

    Key key = acl_map->at(keyName).first;

    if (!std::holds_alternative<std::string>(map->at(key))) {
        throw std::logic_error { "Wrong value type in Settings::setConsoleInputAccessMapping: expected std::string" };
    }

    map->at(key) = value;
}

void Settings::setConsoleInputAccessMapping(const ComposedKey& keyName, int value) {
    auto [map, acl_map] = boost::synchronize(SettingsMap, InputAccessMapping);
    if (!acl_map->contains(keyName)) {
        throw std::logic_error { "Unknown key name accessed in Settings::setConsoleInputAccessMapping" };
    } else if (acl_map->at(keyName).second == SettingsAccessMask::NO_ACCESS) {
        throw std::logic_error { "Key '" + keyName.Category + "::" + keyName.Key + "' is not accessible from within the runtime!" };
    } else if (acl_map->at(keyName).second == SettingsAccessMask::READ_ONLY) {
        throw std::logic_error { "Key '" + keyName.Category + "::" + keyName.Key + "' is not writeable from within the runtime!" };
    }

    Key key = acl_map->at(keyName).first;

    if (!std::holds_alternative<int>(map->at(key))) {
        throw std::logic_error { "Wrong value type in Settings::setConsoleInputAccessMapping: expected int" };
    }

    map->at(key) = value;
}

void Settings::setConsoleInputAccessMapping(const ComposedKey& keyName, bool value) {
    auto [map, acl_map] = boost::synchronize(SettingsMap, InputAccessMapping);
    if (!acl_map->contains(keyName)) {
        throw std::logic_error { "Unknown key name accessed in Settings::setConsoleInputAccessMapping" };
    } else if (acl_map->at(keyName).second == SettingsAccessMask::NO_ACCESS) {
        throw std::logic_error { "Key '" + keyName.Category + "::" + keyName.Key + "' is not accessible from within the runtime!" };
    } else if (acl_map->at(keyName).second == SettingsAccessMask::READ_ONLY) {
        throw std::logic_error { "Key '" + keyName.Category + "::" + keyName.Key + "' is not writeable from within the runtime!" };
    }

    Key key = acl_map->at(keyName).first;

    if (!std::holds_alternative<bool>(map->at(key))) {
        throw std::logic_error { "Wrong value type in Settings::setConsoleInputAccessMapping: expected bool" };
    }

    map->at(key) = value;
}

bool Settings::setByKeyString(const ComposedKey& keyName, const std::string& value, std::string& error) {
    auto [map, acl_map] = boost::synchronize(SettingsMap, InputAccessMapping);
    if (!acl_map->contains(keyName)) {
        error = "Unknown setting '" + keyName.Category + "." + keyName.Key + "'";
        return false;
    }

    const Key key = acl_map->at(keyName).first;
    auto& Slot = map->at(key);

    if (std::holds_alternative<std::string>(Slot)) {
        Slot = value;
        return true;
    }

    if (std::holds_alternative<int>(Slot)) {
        int Parsed = 0;
        const auto* Begin = value.data();
        const auto* End = value.data() + value.size();
        const auto Result = std::from_chars(Begin, End, Parsed);
        if (Result.ec != std::errc() || Result.ptr != End) {
            error = "Expected integer for setting '" + keyName.Category + "." + keyName.Key + "'";
            return false;
        }
        Slot = Parsed;
        return true;
    }

    if (std::holds_alternative<bool>(Slot)) {
        if (value == "true" || value == "1") {
            Slot = true;
            return true;
        }
        if (value == "false" || value == "0") {
            Slot = false;
            return true;
        }
        error = "Expected boolean for setting '" + keyName.Category + "." + keyName.Key + "'";
        return false;
    }

    error = "Unsupported setting type for '" + keyName.Category + "." + keyName.Key + "'";
    return false;
}

TEST_CASE("settings get/set") {
    Settings settings;
    settings.set(Settings::General_Name, "hello, world");
    CHECK_EQ(settings.getAsString(Settings::General_Name), "hello, world");
    settings.set(Settings::General_Name, std::string("hello, world"));
    CHECK_EQ(settings.getAsString(Settings::General_Name), "hello, world");
    settings.set(Settings::General_MaxPlayers, 12);
    CHECK_EQ(settings.getAsInt(Settings::General_MaxPlayers), 12);
}

TEST_CASE("settings check for exception on wrong input type") {
    Settings settings;
    CHECK_THROWS(settings.set(Settings::General_Debug, "hello, world"));
    CHECK_NOTHROW(settings.set(Settings::General_Debug, false));
}
