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

#include "Common.h"

#include "ArgsParser.h"
#include "Env.h"
#include "Settings.h"
#include "TConfig.h"
#include <chrono>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <istream>
#include <sstream>
#include <type_traits>

// General
static constexpr std::string_view StrDebug = "Debug";
static constexpr std::string_view EnvStrDebug = "BEAMMP_DEBUG";
static constexpr std::string_view StrPrivate = "Private";
static constexpr std::string_view EnvStrPrivate = "BEAMMP_PRIVATE";
static constexpr std::string_view StrIP = "IP";
static constexpr std::string_view EnvStrIP = "BEAMMP_IP";
static constexpr std::string_view StrPort = "Port";
static constexpr std::string_view EnvStrPort = "BEAMMP_PORT";
static constexpr std::string_view StrMaxCars = "MaxCars";
static constexpr std::string_view EnvStrMaxCars = "BEAMMP_MAX_CARS";
static constexpr std::string_view StrMaxPlayers = "MaxPlayers";
static constexpr std::string_view EnvStrMaxPlayers = "BEAMMP_MAX_PLAYERS";
static constexpr std::string_view StrMap = "Map";
static constexpr std::string_view EnvStrMap = "BEAMMP_MAP";
static constexpr std::string_view StrName = "Name";
static constexpr std::string_view EnvStrName = "BEAMMP_NAME";
static constexpr std::string_view StrDescription = "Description";
static constexpr std::string_view EnvStrDescription = "BEAMMP_DESCRIPTION";
static constexpr std::string_view StrTags = "Tags";
static constexpr std::string_view EnvStrTags = "BEAMMP_TAGS";
static constexpr std::string_view StrResourceFolder = "ResourceFolder";
static constexpr std::string_view EnvStrResourceFolder = "BEAMMP_RESOURCE_FOLDER";
static constexpr std::string_view StrAuthKey = "AuthKey";
static constexpr std::string_view EnvStrAuthKey = "BEAMMP_AUTH_KEY";
static constexpr std::string_view StrLogChat = "LogChat";
static constexpr std::string_view EnvStrLogChat = "BEAMMP_LOG_CHAT";
static constexpr std::string_view StrAllowGuests = "AllowGuests";
static constexpr std::string_view EnvStrAllowGuests = "BEAMMP_ALLOW_GUESTS";
static constexpr std::string_view StrInformationPacket = "InformationPacket";
static constexpr std::string_view EnvStrInformationPacket = "BEAMMP_INFORMATION_PACKET";
static constexpr std::string_view StrPassword = "Password";

// HttpApi
static constexpr std::string_view StrHttpApiEnabled = "Enabled";
static constexpr std::string_view EnvStrHttpApiEnabled = "BEAMMP_HTTP_API_ENABLED";
static constexpr std::string_view StrHttpApiHost = "Host";
static constexpr std::string_view EnvStrHttpApiHost = "BEAMMP_HTTP_API_HOST";
static constexpr std::string_view StrHttpApiPort = "Port";
static constexpr std::string_view EnvStrHttpApiPort = "BEAMMP_HTTP_API_PORT";
static constexpr std::string_view StrHttpApiToken = "Token";
static constexpr std::string_view EnvStrHttpApiToken = "BEAMMP_HTTP_API_TOKEN";

// SpatialRebase
static constexpr std::string_view StrSpatialRebaseAutoSafeLimitMeters = "AutoSafeLimitMeters";
static constexpr std::string_view EnvStrSpatialRebaseAutoSafeLimitMeters = "BEAMMP_SPATIAL_REBASE_AUTO_SAFE_LIMIT_METERS";
static constexpr std::string_view StrSpatialRebaseAutoRetriggerBandMeters = "AutoRetriggerBandMeters";
static constexpr std::string_view EnvStrSpatialRebaseAutoRetriggerBandMeters = "BEAMMP_SPATIAL_REBASE_AUTO_RETRIGGER_BAND_METERS";
static constexpr std::string_view StrSpatialRebaseAutoCooldownMs = "AutoCooldownMs";
static constexpr std::string_view EnvStrSpatialRebaseAutoCooldownMs = "BEAMMP_SPATIAL_REBASE_AUTO_COOLDOWN_MS";

// Misc
static constexpr std::string_view StrHideUpdateMessages = "ImScaredOfUpdates";
static constexpr std::string_view EnvStrHideUpdateMessages = "BEAMMP_IM_SCARED_OF_UPDATES";
static constexpr std::string_view StrUpdateReminderTime = "UpdateReminderTime";
static constexpr std::string_view EnvStrUpdateReminderTime = "BEAMMP_UPDATE_REMINDER_TIME";

namespace {
struct ConfigSetting {
    std::string_view Category;
    std::string_view Name;
    std::string_view Environment;
    Settings::Key Key;
};

// Both startup overrides and the config loader use this registry.
constexpr ConfigSetting ConfigSettings[] = {
    { "General", StrDebug, EnvStrDebug, Settings::General_Debug },
    { "General", StrPrivate, EnvStrPrivate, Settings::General_Private },
    { "General", StrInformationPacket, EnvStrInformationPacket, Settings::General_InformationPacket },
    { "General", StrPort, EnvStrPort, Settings::General_Port },
    { "General", StrIP, EnvStrIP, Settings::General_IP },
    { "General", StrMaxCars, EnvStrMaxCars, Settings::General_MaxCars },
    { "General", StrMaxPlayers, EnvStrMaxPlayers, Settings::General_MaxPlayers },
    { "General", StrMap, EnvStrMap, Settings::General_Map },
    { "General", StrName, EnvStrName, Settings::General_Name },
    { "General", StrDescription, EnvStrDescription, Settings::General_Description },
    { "General", StrTags, EnvStrTags, Settings::General_Tags },
    { "General", StrResourceFolder, EnvStrResourceFolder, Settings::General_ResourceFolder },
    { "General", StrAuthKey, EnvStrAuthKey, Settings::General_AuthKey },
    { "General", StrLogChat, EnvStrLogChat, Settings::General_LogChat },
    { "General", StrAllowGuests, EnvStrAllowGuests, Settings::General_AllowGuests },
    { "HttpApi", StrHttpApiEnabled, EnvStrHttpApiEnabled, Settings::HttpApi_Enabled },
    { "HttpApi", StrHttpApiHost, EnvStrHttpApiHost, Settings::HttpApi_Host },
    { "HttpApi", StrHttpApiPort, EnvStrHttpApiPort, Settings::HttpApi_Port },
    { "HttpApi", StrHttpApiToken, EnvStrHttpApiToken, Settings::HttpApi_Token },
    { "SpatialRebase", StrSpatialRebaseAutoSafeLimitMeters, EnvStrSpatialRebaseAutoSafeLimitMeters, Settings::SpatialRebase_AutoSafeLimitMeters },
    { "SpatialRebase", StrSpatialRebaseAutoRetriggerBandMeters, EnvStrSpatialRebaseAutoRetriggerBandMeters, Settings::SpatialRebase_AutoRetriggerBandMeters },
    { "SpatialRebase", StrSpatialRebaseAutoCooldownMs, EnvStrSpatialRebaseAutoCooldownMs, Settings::SpatialRebase_AutoCooldownMs },
    { "Misc", StrHideUpdateMessages, EnvStrHideUpdateMessages, Settings::Misc_ImScaredOfUpdates },
    { "Misc", StrUpdateReminderTime, EnvStrUpdateReminderTime, Settings::Misc_UpdateReminderTime },
};

std::string EnvironmentForSetting(const ConfigSetting& Setting) {
    if (Setting.Key == Settings::General_Port) {
        return Env::Get(Env::Key::PROVIDER_PORT_ENV).value_or(std::string(Setting.Environment));
    }
    if (Setting.Key == Settings::General_IP) {
        return Env::Get(Env::Key::PROVIDER_IP_ENV).value_or(std::string(Setting.Environment));
    }
    return std::string(Setting.Environment);
}
}

bool TConfig::ApplyStartupSettingOverride(std::string_view Assignment) {
    const auto Equals = Assignment.find('=');
    if (Equals == std::string_view::npos || Equals == 0 || Equals == Assignment.size() - 1) {
        beammp_error("Invalid --setting override. Expected Section.Key=value");
        return false;
    }
    const auto RawKey = Assignment.substr(0, Equals);
    const auto Dot = RawKey.find('.');
    if (Dot == std::string_view::npos || Dot == 0 || Dot == RawKey.size() - 1) {
        beammp_error("Invalid --setting override key. Expected Section.Key");
        return false;
    }
    const ComposedKey Key { std::string(RawKey.substr(0, Dot)), std::string(RawKey.substr(Dot + 1)) };
    const std::string Value(Assignment.substr(Equals + 1));
    for (const auto& Setting : ConfigSettings) {
        if (Setting.Category != Key.Category || Setting.Name != Key.Key) {
            continue;
        }
        // Validate against the same types used by the loader before changing the environment.
        Settings ValidationSettings;
        std::string Error;
        if (!ValidationSettings.setByKeyString(Key, Value, Error)) {
            beammp_error(Error);
            return false;
        }
        if (!Env::Set(EnvironmentForSetting(Setting), Value)) {
            beammp_error("Failed applying --setting override via environment for '" + std::string(RawKey) + "'");
            return false;
        }
        beammp_info("Startup override applied: " + std::string(RawKey));
        return true;
    }
    beammp_error("Unknown --setting override key '" + std::string(RawKey) + "'");
    return false;
}

TEST_CASE("startup setting overrides") {
    struct StartupFixture {
        std::unordered_map<std::string, std::string> Environment;
        std::unordered_map<Settings::Key, Settings::SettingsTypeVariant> OriginalSettings = *Application::Settings.SettingsMap;
        const std::string File = (fs::temp_directory_path() / ("beammp-startup-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".toml")).string();

        void Clear(std::string Name) {
            const auto* Value = std::getenv(Name.c_str());
            Environment.emplace(Name, Value ? Value : "");
            Env::Set(Name, "");
        }

        StartupFixture() {
            // Save redirected provider variables before removing the redirections.
            for (const auto& Setting : ConfigSettings) {
                Clear(EnvironmentForSetting(Setting));
                if (!Environment.contains(std::string(Setting.Environment))) {
                    Clear(std::string(Setting.Environment));
                }
            }
            Clear(std::string(Env::ToString(Env::Key::PROVIDER_PORT_ENV)));
            Clear(std::string(Env::ToString(Env::Key::PROVIDER_IP_ENV)));
            Clear(std::string(Env::ToString(Env::Key::PROVIDER_DISABLE_CONFIG)));
            Settings Defaults;
            Application::Settings.SettingsMap = *Defaults.SettingsMap;
            Env::Set(Env::Key::PROVIDER_DISABLE_CONFIG, "true");
            Env::Set(EnvStrAuthKey, "00000000-0000-0000-0000-000000000000");
        }

        ~StartupFixture() {
            for (const auto& [Name, Value] : Environment) {
                Env::Set(Name, Value);
            }
            Application::Settings.SettingsMap = OriginalSettings;
            fs::remove(File);
        }
    } Fixture;

    SUBCASE("all registered settings reach the active settings without TOML") {
        Settings Expected;
        REQUIRE_EQ(std::size(ConfigSettings), Expected.getAccessControlMap().size());
        for (const auto& Setting : ConfigSettings) {
            const auto Default = Expected.get(Setting.Key);
            std::string Value = "custom value=with spaces";
            if (std::holds_alternative<bool>(Default)) {
                Value = "false";
            } else if (std::holds_alternative<int>(Default)) {
                Value = "23";
            }
            const ComposedKey Key { std::string(Setting.Category), std::string(Setting.Name) };
            std::string Error;
            REQUIRE(Expected.setByKeyString(Key, Value, Error));
            REQUIRE(TConfig::ApplyStartupSettingOverride(Key.Category + "." + Key.Key + "=" + Value));
        }
        TConfig Config(Fixture.File);
        REQUIRE_FALSE(Config.Failed());
        CHECK_FALSE(fs::exists(Fixture.File));
        for (const auto& Setting : ConfigSettings) {
            CHECK(Application::Settings.get(Setting.Key) == Expected.get(Setting.Key));
        }
    }

    SUBCASE("last repeated CLI value wins over environment and TOML") {
        Env::Set(Env::Key::PROVIDER_DISABLE_CONFIG, "false");
        Application::Settings.set(Settings::General_Description, std::string("TOML description"));
        TConfig InitialConfig(Fixture.File);
        REQUIRE_FALSE(InitialConfig.Failed());
        Env::Set(EnvStrDescription, "environment description");
        ArgsParser Parser;
        Parser.RegisterArgument({ "setting" }, ArgsParser::HAS_VALUE);
        Parser.Parse({ "--setting=General.Description=first", "--setting=General.Description=last value=a=b", "--setting=General.Private=false", "--setting=General.MaxPlayers=23" });
        REQUIRE(Parser.Verify());
        for (const auto& Value : Parser.GetValuesOfArgument({ "setting" })) {
            REQUIRE(TConfig::ApplyStartupSettingOverride(Value));
        }
        TConfig Config(Fixture.File);
        REQUIRE_FALSE(Config.Failed());
        CHECK_EQ(Application::Settings.getAsString(Settings::General_Description), "last value=a=b");
        CHECK_FALSE(Application::Settings.getAsBool(Settings::General_Private));
        CHECK_EQ(Application::Settings.getAsInt(Settings::General_MaxPlayers), 23);
    }

    SUBCASE("unknown keys and invalid values are rejected without changing the environment") {
        Env::Set(EnvStrMaxPlayers, "17");
        for (const auto* Assignment : { "Unknown.Name=value", "General.Unknown=value", "General.Name.Extra=value", "Name=value", ".Name=value", "General.=value", "General.Name", "General.Name=", "General.Private=maybe", "General.MaxPlayers=23junk", "General.MaxPlayers=99999999999999999999" }) {
            CHECK_FALSE(TConfig::ApplyStartupSettingOverride(Assignment));
        }
        CHECK_EQ(std::string(std::getenv(EnvStrMaxPlayers.data())), "17");
    }

    SUBCASE("provider IP and port mappings honor CLI overrides") {
        Fixture.Clear("BEAMMP_TEST_STARTUP_PORT");
        Fixture.Clear("BEAMMP_TEST_STARTUP_IP");
        Env::Set(Env::Key::PROVIDER_PORT_ENV, "BEAMMP_TEST_STARTUP_PORT");
        Env::Set(Env::Key::PROVIDER_IP_ENV, "BEAMMP_TEST_STARTUP_IP");
        Env::Set("BEAMMP_TEST_STARTUP_PORT", "12345");
        Env::Set("BEAMMP_TEST_STARTUP_IP", "0.0.0.0");
        REQUIRE(TConfig::ApplyStartupSettingOverride("General.Port=23456"));
        REQUIRE(TConfig::ApplyStartupSettingOverride("General.IP=127.0.0.1"));
        TConfig Config(Fixture.File);
        REQUIRE_FALSE(Config.Failed());
        CHECK_EQ(Application::Settings.getAsInt(Settings::General_Port), 23456);
        CHECK_EQ(Application::Settings.getAsString(Settings::General_IP), "127.0.0.1");
    }
}

TEST_CASE("TConfig::TConfig") {
    const std::string CfgFile = "beammp_server_testconfig.toml";
    fs::remove(CfgFile);

    TConfig Cfg(CfgFile);

    CHECK(fs::file_size(CfgFile) != 0);

    std::string buf;
    {
        buf.resize(fs::file_size(CfgFile));
        auto fp = std::fopen(CfgFile.c_str(), "r");
        auto res = std::fread(buf.data(), 1, buf.size(), fp);
        if (res != buf.size()) {
            // IGNORE?
        }
        std::fclose(fp);
    }
    INFO("file contents are:", buf);

    const auto table = toml::parse(CfgFile);
    CHECK(table.at("General").is_table());
    CHECK(table.at("SpatialRebase").is_table());
    CHECK(table.at("Misc").is_table());

    fs::remove(CfgFile);
}

TConfig::TConfig(const std::string& ConfigFileName)
    : mConfigFileName(ConfigFileName) {
    Application::SetSubsystemStatus("Config", Application::Status::Starting);
    auto DisableConfig = Env::Get(Env::Key::PROVIDER_DISABLE_CONFIG).value_or("false");
    mDisableConfig = DisableConfig == "true" || DisableConfig == "1";
    if (!mDisableConfig && (!fs::exists(mConfigFileName) || !fs::is_regular_file(mConfigFileName))) {
        beammp_info("No config file found! Generating one...");
        CreateConfigFile();
    }
    if (!mFailed) {
        if (fs::exists("Server.cfg")) {
            beammp_warn("An old \"Server.cfg\" file still exists. Please note that this is no longer used. Instead, \"" + std::string(mConfigFileName) + "\" is used. You can safely delete the \"Server.cfg\".");
        }
        ParseFromFile(mConfigFileName);
    }
}

template <typename CommentsT>
void SetComment(CommentsT& Comments, const std::string& Comment) {
    Comments.clear();
    Comments.push_back(Comment);
}

/**
 * @brief Writes out the loaded application state into ServerConfig.toml
 *
 * This writes out the current state of application settings that are
 * applied to the server instance (i.e. the current application settings loaded in the server).
 * If the state of the application settings changes during runtime,
 * call this function whenever something about the config changes
 * whether it is in TConfig.cpp or the configuration file.
 */
void TConfig::FlushToFile() {
    // auto data = toml::parse<toml::preserve_comments>(mConfigFileName);
    auto data = toml::value {};
    data["General"][StrAuthKey.data()] = Application::Settings.getAsString(Settings::Key::General_AuthKey);
    SetComment(data["General"][StrAuthKey.data()].comments(), " AuthKey has to be filled out in order to run the server");
    data["General"][StrLogChat.data()] = Application::Settings.getAsBool(Settings::Key::General_LogChat);
    SetComment(data["General"][StrLogChat.data()].comments(), " Whether to log chat messages in the console / log");
    data["General"][StrDebug.data()] = Application::Settings.getAsBool(Settings::Key::General_Debug);
    data["General"][StrPrivate.data()] = Application::Settings.getAsBool(Settings::Key::General_Private);
    SetComment(data["General"][StrInformationPacket.data()].comments(), " Whether to allow unconnected clients to get the public server information without joining");
    data["General"][StrInformationPacket.data()] = Application::Settings.getAsBool(Settings::Key::General_InformationPacket);
    data["General"][StrAllowGuests.data()] = Application::Settings.getAsBool(Settings::Key::General_AllowGuests);
    SetComment(data["General"][StrAllowGuests.data()].comments(), " Whether to allow guests");
    data["General"][StrIP.data()] = Application::Settings.getAsString(Settings::Key::General_IP);
    SetComment(data["General"][StrIP.data()].comments(), " The IP address to bind the server to, this is NOT related to your public IP. Can be used if your machine has multiple network interfaces");
    data["General"][StrPort.data()] = Application::Settings.getAsInt(Settings::Key::General_Port);
    data["General"][StrName.data()] = Application::Settings.getAsString(Settings::Key::General_Name);
    SetComment(data["General"][StrTags.data()].comments(), " Add custom identifying tags to your server to make it easier to find. Format should be TagA,TagB,TagC. Note the comma seperation.");
    data["General"][StrTags.data()] = Application::Settings.getAsString(Settings::Key::General_Tags);
    data["General"][StrMaxCars.data()] = Application::Settings.getAsInt(Settings::Key::General_MaxCars);
    data["General"][StrMaxPlayers.data()] = Application::Settings.getAsInt(Settings::Key::General_MaxPlayers);
    data["General"][StrMap.data()] = Application::Settings.getAsString(Settings::Key::General_Map);
    data["General"][StrDescription.data()] = Application::Settings.getAsString(Settings::Key::General_Description);
    data["General"][StrResourceFolder.data()] = Application::Settings.getAsString(Settings::Key::General_ResourceFolder);
    // data["General"][StrPassword.data()] = Application::Settings.Password;
    // SetComment(data["General"][StrPassword.data()].comments(), " Sets a password on this server, which restricts people from joining. To join, a player must enter this exact password. Leave empty ("") to disable the password.");
    // HttpApi
    data["HttpApi"][StrHttpApiEnabled.data()] = Application::Settings.getAsBool(Settings::Key::HttpApi_Enabled);
    SetComment(data["HttpApi"][StrHttpApiEnabled.data()].comments(), " Whether to enable the built-in HTTP API server");
    data["HttpApi"][StrHttpApiHost.data()] = Application::Settings.getAsString(Settings::Key::HttpApi_Host);
    SetComment(data["HttpApi"][StrHttpApiHost.data()].comments(), " The bind address for the HTTP API. Use 127.0.0.1 to keep it local-only by default");
    data["HttpApi"][StrHttpApiPort.data()] = Application::Settings.getAsInt(Settings::Key::HttpApi_Port);
    SetComment(data["HttpApi"][StrHttpApiPort.data()].comments(), " The TCP port for the HTTP API");
    data["HttpApi"][StrHttpApiToken.data()] = Application::Settings.getAsString(Settings::Key::HttpApi_Token);
    SetComment(data["HttpApi"][StrHttpApiToken.data()].comments(), " Optional bearer token for the HTTP API. Leave empty to allow loopback-only access without a token");
    // SpatialRebase
    data["SpatialRebase"][StrSpatialRebaseAutoSafeLimitMeters.data()] = Application::Settings.getAsInt(Settings::Key::SpatialRebase_AutoSafeLimitMeters);
    SetComment(data["SpatialRebase"][StrSpatialRebaseAutoSafeLimitMeters.data()].comments(), " Deprecated in this fork. Automatic rebasing is externally orchestrated; keep this at 0.");
    data["SpatialRebase"][StrSpatialRebaseAutoRetriggerBandMeters.data()] = Application::Settings.getAsInt(Settings::Key::SpatialRebase_AutoRetriggerBandMeters);
    SetComment(data["SpatialRebase"][StrSpatialRebaseAutoRetriggerBandMeters.data()].comments(), " Deprecated in this fork. Automatic rebasing is externally orchestrated.");
    data["SpatialRebase"][StrSpatialRebaseAutoCooldownMs.data()] = Application::Settings.getAsInt(Settings::Key::SpatialRebase_AutoCooldownMs);
    SetComment(data["SpatialRebase"][StrSpatialRebaseAutoCooldownMs.data()].comments(), " Deprecated in this fork. Automatic rebasing is externally orchestrated.");
    // Misc
    data["Misc"][StrHideUpdateMessages.data()] = Application::Settings.getAsBool(Settings::Key::Misc_ImScaredOfUpdates);
    SetComment(data["Misc"][StrHideUpdateMessages.data()].comments(), " Hides the periodic update message which notifies you of a new server version. You should really keep this on and always update as soon as possible. For more information visit https://wiki.beammp.com/en/home/server-maintenance#updating-the-server. An update message will always appear at startup regardless.");
    data["Misc"][StrUpdateReminderTime.data()] = Application::Settings.getAsString(Settings::Key::Misc_UpdateReminderTime);
    SetComment(data["Misc"][StrUpdateReminderTime.data()].comments(), " Specifies the time between update reminders. You can use any of \"s, min, h, d\" at the end to specify the units seconds, minutes, hours or days. So 30d or 0.5min will print the update message every 30 days or half a minute.");
    std::stringstream Ss;
    Ss << "# This is the BeamMP-Server config file.\n"
          "# Help & Documentation: `https://docs.beammp.com/server/server-maintenance/`\n"
          "# IMPORTANT: Fill in the AuthKey with the key you got from `https://keymaster.beammp.com/` on the left under \"Keys\"\n"
       << toml::format(data);
    auto File = std::fopen(mConfigFileName.c_str(), "w+");
    if (!File) {
        beammp_error("Failed to create/write to config file: " + GetPlatformAgnosticErrorString());
        throw std::runtime_error("Failed to create/write to config file");
    }
    auto Str = Ss.str();
    auto N = std::fwrite(Str.data(), sizeof(char), Str.size(), File);
    if (N != Str.size()) {
        beammp_error("Failed to write to config file properly, config file might be misshapen");
    }
    std::fclose(File);
}

void TConfig::CreateConfigFile() {
    // build from old config Server.cfg
    if (mDisableConfig) {
        return;
    }
    FlushToFile();
}

// This arcane template magic is needed for using lambdas as overloaded visitors
// See https://en.cppreference.com/w/cpp/utility/variant/visit for reference
template <class... Ts>
struct overloaded : Ts... {
    using Ts::operator()...;
};
template <class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

void TConfig::TryReadValue(toml::value& Table, const std::string& Category, const std::string_view& Key, const std::string_view& Env, Settings::Key key) {
    if (!Env.empty()) {
        if (const char* envp = std::getenv(Env.data());
            envp != nullptr && std::strcmp(envp, "") != 0) {

            std::visit(
                overloaded {
                    [&envp, &key](std::string) {
                        Application::Settings.set(key, std::string(envp));
                    },
                    [&envp, &key](int) {
                        Application::Settings.set(key, int(std::strtol(envp, nullptr, 10)));
                    },
                    [&envp, &key](bool) {
                        auto Str = std::string(envp);
                        Application::Settings.set(key, bool(Str == "1" || Str == "true"));
                    } },

                Application::Settings.get(key));
            return;
        }
    }

    if (mDisableConfig) {
        return;
    }

    std::visit([&Table, &Category, &Key, &key](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::string>) {
            if (Table[Category.c_str()][Key.data()].is_string())
                Application::Settings.set(key, Table[Category.c_str()][Key.data()].as_string());
            else
                beammp_warnf("Value '{}.{}' has unexpected type, expected type 'string'", Category, Key);
        } else if constexpr (std::is_same_v<T, int>) {
            if (Table[Category.c_str()][Key.data()].is_integer())
                Application::Settings.set(key, int(Table[Category.c_str()][Key.data()].as_integer()));
            else
                beammp_warnf("Value '{}.{}' has unexpected type, expected type 'integer'", Category, Key);
        } else if constexpr (std::is_same_v<T, bool>) {
            if (Table[Category.c_str()][Key.data()].is_boolean())
                Application::Settings.set(key, Table[Category.c_str()][Key.data()].as_boolean());
            else
                beammp_warnf("Value '{}.{}' has unexpected type, expected type 'boolean'", Category, Key);
        } else {
            throw std::logic_error { "Invalid type for config value during read attempt" };
        }
    },
        Application::Settings.get(key));
}

void TConfig::ParseFromFile(std::string_view name) {
    try {
        toml::value data {};
        if (!mDisableConfig) {
            data = toml::parse(name.data());
        }

        for (const auto& Setting : ConfigSettings) {
            TryReadValue(data, std::string(Setting.Category), Setting.Name, EnvironmentForSetting(Setting), Setting.Key);
        }

    } catch (const std::exception& err) {
        beammp_error("Error parsing config file value: " + std::string(err.what()));
        mFailed = true;
        Application::SetSubsystemStatus("Config", Application::Status::Bad);
        return;
    }

    // Update in any case
    if (!mDisableConfig) {
        FlushToFile();
    }
    // all good so far, let's check if there's a key
    if (Application::Settings.getAsString(Settings::Key::General_AuthKey).empty()) {
        if (mDisableConfig) {
            beammp_error("No AuthKey specified in the environment.");
        } else {
            beammp_error("No AuthKey specified in the \"" + std::string(mConfigFileName) + "\" file. Please get an AuthKey, enter it into the config file, and restart this server.");
        }
        Application::SetSubsystemStatus("Config", Application::Status::Bad);
        mFailed = true;
        return;
    }
    Application::SetSubsystemStatus("Config", Application::Status::Good);
    if (Application::Settings.getAsString(Settings::Key::General_AuthKey).size() != 36) {
        beammp_warn("AuthKey specified is the wrong length and likely isn't valid.");
    }
}

void TConfig::PrintDebug() {
    if (mDisableConfig) {
        beammp_debug("Provider turned off the generation and parsing of the ServerConfig.toml");
    }
    beammp_debug(std::string(StrDebug) + ": " + std::string(Application::Settings.getAsBool(Settings::Key::General_Debug) ? "true" : "false"));
    beammp_debug(std::string(StrPrivate) + ": " + std::string(Application::Settings.getAsBool(Settings::Key::General_Private) ? "true" : "false"));
    beammp_debug(std::string(StrInformationPacket) + ": " + std::string(Application::Settings.getAsBool(Settings::Key::General_InformationPacket) ? "true" : "false"));
    beammp_debug(std::string(StrPort) + ": " + std::to_string(Application::Settings.getAsInt(Settings::Key::General_Port)));
    beammp_debug(std::string(StrIP) + ": \"" + Application::Settings.getAsString(Settings::Key::General_IP) + "\"");
    beammp_debug(std::string(StrMaxCars) + ": " + std::to_string(Application::Settings.getAsInt(Settings::Key::General_MaxCars)));
    beammp_debug(std::string(StrMaxPlayers) + ": " + std::to_string(Application::Settings.getAsInt(Settings::Key::General_MaxPlayers)));
    beammp_debug(std::string(StrMap) + ": \"" + Application::Settings.getAsString(Settings::Key::General_Map) + "\"");
    beammp_debug(std::string(StrName) + ": \"" + Application::Settings.getAsString(Settings::Key::General_Name) + "\"");
    beammp_debug(std::string(StrDescription) + ": \"" + Application::Settings.getAsString(Settings::Key::General_Description) + "\"");
    beammp_debug(std::string(StrTags) + ": " + TagsAsPrettyArray());
    beammp_debug(std::string(StrLogChat) + ": \"" + (Application::Settings.getAsBool(Settings::Key::General_LogChat) ? "true" : "false") + "\"");
    beammp_debug(std::string(StrResourceFolder) + ": \"" + Application::Settings.getAsString(Settings::Key::General_ResourceFolder) + "\"");
    beammp_debug(std::string(StrAllowGuests) + ": \"" + (Application::Settings.getAsBool(Settings::Key::General_AllowGuests) ? "true" : "false") + "\"");
    beammp_debug(std::string("HttpApi.") + std::string(StrHttpApiEnabled) + ": " + std::string(Application::Settings.getAsBool(Settings::Key::HttpApi_Enabled) ? "true" : "false"));
    beammp_debug(std::string("HttpApi.") + std::string(StrHttpApiHost) + ": \"" + Application::Settings.getAsString(Settings::Key::HttpApi_Host) + "\"");
    beammp_debug(std::string("HttpApi.") + std::string(StrHttpApiPort) + ": " + std::to_string(Application::Settings.getAsInt(Settings::Key::HttpApi_Port)));
    beammp_debug(std::string("SpatialRebase.") + std::string(StrSpatialRebaseAutoSafeLimitMeters) + ": " + std::to_string(Application::Settings.getAsInt(Settings::Key::SpatialRebase_AutoSafeLimitMeters)));
    beammp_debug(std::string("SpatialRebase.") + std::string(StrSpatialRebaseAutoRetriggerBandMeters) + ": " + std::to_string(Application::Settings.getAsInt(Settings::Key::SpatialRebase_AutoRetriggerBandMeters)));
    beammp_debug(std::string("SpatialRebase.") + std::string(StrSpatialRebaseAutoCooldownMs) + ": " + std::to_string(Application::Settings.getAsInt(Settings::Key::SpatialRebase_AutoCooldownMs)));
    // special!
    beammp_debug("Key Length: " + std::to_string(Application::Settings.getAsString(Settings::Key::General_AuthKey).length()) + "");
}

std::string TConfig::TagsAsPrettyArray() const {
    std::vector<std::string> TagsArray = {};
    SplitString(Application::Settings.getAsString(Settings::General_Tags), ',', TagsArray);
    std::string Pretty = {};
    for (size_t i = 0; i < TagsArray.size() - 1; ++i) {
        Pretty += '\"' + TagsArray[i] + "\", ";
    }
    Pretty += '\"' + TagsArray.at(TagsArray.size() - 1) + "\"";
    return Pretty;
}
