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

#include "ArgsParser.h"
#include "Common.h"
#include "Env.h"
#include "Http.h"
#include "LuaAPI.h"
#include "Settings.h"
#include "SignalHandling.h"
#include "TConfig.h"
#include "TControlService.h"
#include "THeartbeatThread.h"
#include "TLuaEngine.h"
#include "TNetwork.h"
#include "TPPSMonitor.h"
#include "TPluginMonitor.h"
#include "TResourceManager.h"
#include "TServer.h"

#include <cstdint>
#include <iostream>
#include <string_view>
#include <thread>

static const std::string sCommandlineArguments = R"(
USAGE:
    BeamMP-Server [arguments]

ARGUMENTS:
    --help
                        Displays this help and exits.
    --port=1234
                        Sets the server's listening TCP and
                        UDP port. Overrides ENV and ServerConfig.
    --config=/path/to/ServerConfig.toml
                        Absolute or relative path to the
                        Server Config file, including the
                        filename. For paths and filenames with
                        spaces, put quotes around the path.
    --working-directory=/path/to/folder
                        Sets the working directory of the Server.
                        All paths are considered relative to this,
                        including the path given in --config.
    --no-config
                        Starts from built-in defaults and startup
                        overrides only, without reading or writing
                        ServerConfig.toml.
    --setting=Section.Key=value
                        Applies a startup setting override before
                        server initialization. Can be repeated.
    --version
                        Prints version info and exits.

EXAMPLES:
    BeamMP-Server --config=../MyWestCoastServerConfig.toml
        Runs the BeamMP-Server and uses the server config file
        which is one directory above it and is named
        'MyWestCoastServerConfig.toml'.
)";

struct MainArguments {
    int argc {};
    char** argv {};
    std::vector<std::string_view> List;
    std::string InvokedAs;
};

int BeamMPServerMain(MainArguments Arguments);

namespace {
std::optional<std::string_view> EnvVarForStartupSetting(const ComposedKey& key)
{
    if (key.Category == "General") {
        if (key.Key == "AuthKey") return "BEAMMP_AUTH_KEY";
        if (key.Key == "Name") return "BEAMMP_NAME";
        if (key.Key == "Map") return "BEAMMP_MAP";
        if (key.Key == "ResourceFolder") return "BEAMMP_RESOURCE_FOLDER";
        if (key.Key == "AllowGuests") return "BEAMMP_ALLOW_GUESTS";
    }
    if (key.Category == "HttpApi") {
        if (key.Key == "Enabled") return "BEAMMP_HTTP_API_ENABLED";
        if (key.Key == "Host") return "BEAMMP_HTTP_API_HOST";
        if (key.Key == "Port") return "BEAMMP_HTTP_API_PORT";
        if (key.Key == "Token") return "BEAMMP_HTTP_API_TOKEN";
    }
    if (key.Category == "SpatialRebase") {
        if (key.Key == "AutoSafeLimitMeters") return "BEAMMP_SPATIAL_REBASE_AUTO_SAFE_LIMIT_METERS";
        if (key.Key == "AutoRetriggerBandMeters") return "BEAMMP_SPATIAL_REBASE_AUTO_RETRIGGER_BAND_METERS";
        if (key.Key == "AutoCooldownMs") return "BEAMMP_SPATIAL_REBASE_AUTO_COOLDOWN_MS";
    }
    return std::nullopt;
}

std::optional<ComposedKey> ParseComposedKey(std::string_view Raw) {
    const auto Dot = Raw.find('.');
    if (Dot == std::string_view::npos || Dot == 0 || Dot == Raw.size() - 1) {
        return std::nullopt;
    }
    return ComposedKey { std::string(Raw.substr(0, Dot)), std::string(Raw.substr(Dot + 1)) };
}

bool ApplyStartupSettingOverride(std::string_view Assignment) {
    const auto Equals = Assignment.find('=');
    if (Equals == std::string_view::npos || Equals == 0 || Equals == Assignment.size() - 1) {
        beammp_error("Invalid --setting override '" + std::string(Assignment) + "'. Expected Section.Key=value");
        return false;
    }

    const auto MaybeKey = ParseComposedKey(Assignment.substr(0, Equals));
    if (!MaybeKey.has_value()) {
        beammp_error("Invalid --setting override key '" + std::string(Assignment.substr(0, Equals)) + "'. Expected Section.Key");
        return false;
    }

    const std::string Value = std::string(Assignment.substr(Equals + 1));

    if (const auto EnvVar = EnvVarForStartupSetting(*MaybeKey); EnvVar.has_value()) {
        if (!Env::Set(*EnvVar, Value)) {
            beammp_error("Failed applying --setting override via environment for '" + MaybeKey->Category + "." + MaybeKey->Key + "'");
            return false;
        }
    }

    beammp_info("Startup override applied: " + MaybeKey->Category + "." + MaybeKey->Key);
    return true;
}
}

int main(int argc, char** argv) {
    MainArguments Args { argc, argv, {}, argv[0] };
    Args.List.reserve(size_t(argc));
    for (int i = 1; i < argc; ++i) {
        Args.List.push_back(argv[i]);
    }
    int MainRet = 0;
    try {
        MainRet = BeamMPServerMain(std::move(Args));
    } catch (const std::exception& e) {
        beammp_error("A fatal exception has occurred and the server is forcefully shutting down.");
        beammp_error(e.what());
        MainRet = -1;
    }
    std::exit(MainRet);
}

int BeamMPServerMain(MainArguments Arguments) {
    setlocale(LC_ALL, "C");
    ArgsParser Parser;
    Parser.RegisterArgument({ "help" }, ArgsParser::NONE);
    Parser.RegisterArgument({ "version" }, ArgsParser::NONE);
    Parser.RegisterArgument({ "config" }, ArgsParser::HAS_VALUE);
    Parser.RegisterArgument({ "port" }, ArgsParser::HAS_VALUE);
    Parser.RegisterArgument({ "no-config" }, ArgsParser::NONE);
    Parser.RegisterArgument({ "setting" }, ArgsParser::HAS_VALUE);
    Parser.RegisterArgument({ "working-directory" }, ArgsParser::HAS_VALUE);
    Parser.Parse(Arguments.List);
    if (!Parser.Verify()) {
        return 1;
    }
    if (Parser.FoundArgument({ "help" })) {
        Application::Console().WriteRaw(sCommandlineArguments);
        return 0;
    }
    if (Parser.FoundArgument({ "version" })) {
        Application::Console().WriteRaw("BeamMP-Server v" + Application::ServerVersionString());
        return 0;
    }

    std::string ConfigPath = "ServerConfig.toml";
    if (Parser.FoundArgument({ "config" })) {
        auto MaybeConfigPath = Parser.GetValueOfArgument({ "config" });
        if (MaybeConfigPath.has_value()) {
            ConfigPath = MaybeConfigPath.value();
            beammp_info("Custom config requested via commandline arguments: '" + ConfigPath + "'");
        }
    }
    if (Parser.FoundArgument({ "working-directory" })) {
        auto MaybeWorkingDirectory = Parser.GetValueOfArgument({ "working-directory" });
        if (MaybeWorkingDirectory.has_value()) {
            beammp_info("Custom working directory requested via commandline arguments: '" + MaybeWorkingDirectory.value() + "'");
            try {
                fs::current_path(fs::path(MaybeWorkingDirectory.value()));
            } catch (const std::exception& e) {
                beammp_errorf("Could not set working directory to '{}': {}", MaybeWorkingDirectory.value(), e.what());
            }
        }
    }

    if (Parser.FoundArgument({ "no-config" })) {
        Env::Set(Env::Key::PROVIDER_DISABLE_CONFIG, "true");
        beammp_info("Startup requested --no-config; skipping ServerConfig.toml reads and writes");
    }

    for (const auto& SettingOverride : Parser.GetValuesOfArgument({ "setting" })) {
        if (!ApplyStartupSettingOverride(SettingOverride)) {
            return 1;
        }
    }

    TConfig Config(ConfigPath);

    if (Config.Failed()) {
        beammp_info("Closing in 10 seconds");
        // loop to make it possible to ctrl+c instead
        for (size_t i = 0; i < 20; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        return 1;
    }

    // override port if provided via arguments
    if (Parser.FoundArgument({ "port" })) {
        auto Port = Parser.GetValueOfArgument({ "port" });
        if (Port.has_value()) {
            auto P = int(std::strtoul(Port.value().c_str(), nullptr, 10));
            if (P == 0 || P < 0 || P > UINT16_MAX) {
                beammp_errorf("Custom port requested via --port is invalid: '{}'", Port.value());
                return 1;
            } else {
                Application::Settings.set(Settings::Key::General_Port, P);
                beammp_info("Custom port requested via commandline arguments: " + Port.value());
            }
        }
    }

    Config.PrintDebug();

    Application::InitializeConsole();
    Application::Console().StartLoggingToFile();

    Application::SetSubsystemStatus("Main", Application::Status::Starting);

    SetupSignalHandlers();

    Settings settings {};
    beammp_infof("Server name set in new impl: {}", settings.getAsString(Settings::Key::General_Name));

    bool Shutdown = false;
    Application::RegisterShutdownHandler([&Shutdown] {
        beammp_info("If this takes too long, you can press Ctrl+C repeatedly to force a shutdown.");
        Application::SetSubsystemStatus("Main", Application::Status::ShuttingDown);
        Shutdown = true;
    });
    Application::RegisterShutdownHandler([] {
        auto Futures = LuaAPI::MP::Engine->TriggerEvent("onShutdown", "");
        TLuaEngine::WaitForAll(Futures, std::chrono::seconds(5));
    });

    TServer Server(Arguments.List);

    RegisterThread("Main");

    beammp_trace("Running in debug mode on a debug build");
    TResourceManager ResourceManager;
    ResourceManager.RefreshFiles();
    TPPSMonitor PPSMonitor(Server);
    THeartbeatThread Heartbeat(ResourceManager, Server);
    TNetwork Network(Server, PPSMonitor, ResourceManager);

    auto LuaEngine = std::make_shared<TLuaEngine>();
    LuaEngine->SetServer(&Server);
    Application::Console().InitializeLuaConsole(*LuaEngine);
    LuaEngine->SetNetwork(&Network);
    PPSMonitor.SetNetwork(Network);
    TControlService ControlService(Server, Network, ResourceManager, *LuaEngine);
    Application::SetControl(&ControlService);
    Http::Server::THttpServerInstance HttpServer;
    Application::CheckForUpdates();

    TPluginMonitor PluginMonitor(fs::path(Application::Settings.getAsString(Settings::Key::General_ResourceFolder)) / "Server", LuaEngine);

    RegisterThread("Main(Waiting)");

    std::set<std::string> IgnoreSubsystems {
        "UpdateCheck" // Ignore as not to confuse users (non-vital system)
    };

    bool FullyStarted = false;
    while (!Shutdown) {
        if (!FullyStarted) {
            FullyStarted = true;
            bool WithErrors = false;
            std::string SystemsBadList {};
            auto Statuses = Application::GetSubsystemStatuses();
            for (const auto& NameStatusPair : Statuses) {
                if (NameStatusPair.first == "Main") {
                    continue;
                }

                if (IgnoreSubsystems.count(NameStatusPair.first) > 0) {
                    continue; // ignore
                }
                if (NameStatusPair.second == Application::Status::Starting) {
                    FullyStarted = false;
                } else if (NameStatusPair.second == Application::Status::Bad) {
                    SystemsBadList += NameStatusPair.first + ", ";
                    WithErrors = true;
                }
            }
            // remove ", "
            SystemsBadList = SystemsBadList.substr(0, SystemsBadList.size() - 2);
            if (FullyStarted) {
                Application::SetSubsystemStatus("Main", Application::Status::Good);

                if (!WithErrors) {
                    beammp_info("ALL SYSTEMS STARTED SUCCESSFULLY, EVERYTHING IS OKAY");
                } else {
                    beammp_error("STARTUP NOT SUCCESSFUL, SYSTEMS " + SystemsBadList + " HAD ERRORS. THIS MAY OR MAY NOT CAUSE ISSUES.");
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    Application::SetSubsystemStatus("Main", Application::Status::Shutdown);
    beammp_info("Shutdown.");
    return 0;
}
