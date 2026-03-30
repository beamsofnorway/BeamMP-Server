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

#include "TConsole.h"
#include "Common.h"
#include "Compat.h"

#include "Client.h"
#include "CustomAssert.h"
#include "LuaAPI.h"
#include "TControlService.h"
#include "TLuaEngine.h"
#include "Http.h"

#include <ctime>
#include <chrono>
#include <lua.hpp>
#include <mutex>
#include <openssl/opensslv.h>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

static inline bool StringStartsWith(const std::string& What, const std::string& StartsWith) {
    return What.size() >= StartsWith.size() && What.substr(0, StartsWith.size()) == StartsWith;
}

TEST_CASE("StringStartsWith") {
    CHECK(StringStartsWith("Hello, World", "Hello"));
    CHECK(StringStartsWith("Hello, World", "H"));
    CHECK(StringStartsWith("Hello, World", ""));
    CHECK(!StringStartsWith("Hello, World", "ello"));
    CHECK(!StringStartsWith("Hello, World", "World"));
    CHECK(StringStartsWith("", ""));
    CHECK(!StringStartsWith("", "hello"));
}

// Trims leading and trailing spaces, newlines, tabs, etc.
static inline std::string TrimString(std::string S) {
    S.erase(S.begin(), std::find_if(S.begin(), S.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
    S.erase(std::find_if(S.rbegin(), S.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(),
        S.end());
    return S;
}

static int64_t UnixTimestampMsNow() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch())
        .count();
}

TEST_CASE("TrimString") {
    CHECK(TrimString("hel lo") == "hel lo");
    CHECK(TrimString(" hel lo") == "hel lo");
    CHECK(TrimString(" hel lo ") == "hel lo");
    CHECK(TrimString("hel lo     ") == "hel lo");
    CHECK(TrimString("     hel lo") == "hel lo");
    CHECK(TrimString("hel lo     ") == "hel lo");
    CHECK(TrimString("    hel lo     ") == "hel lo");
    CHECK(TrimString("\t\thel\nlo\n\n") == "hel\nlo");
    CHECK(TrimString("\n\thel\tlo\n\t") == "hel\tlo");
    CHECK(TrimString("  ") == "");
    CHECK(TrimString(" \t\n\r ") == "");
    CHECK(TrimString("") == "");
}

static std::string GetDate() {
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    time_t tt = std::chrono::system_clock::to_time_t(now);
    auto local_tm = std::localtime(&tt);
    char buf[30];
    std::string date;
    if (Application::Settings.getAsBool(Settings::Key::General_Debug)) {
        std::strftime(buf, sizeof(buf), "[%d/%m/%y %T.", local_tm);
        date += buf;
        auto seconds = std::chrono::time_point_cast<std::chrono::seconds>(now);
        auto fraction = now - seconds;
        size_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(fraction).count();
        char fracstr[5];
        std::sprintf(fracstr, "%03lu", ms);
        date += fracstr;
        date += "] ";
    } else {
        std::strftime(buf, sizeof(buf), "[%d/%m/%y %T] ", local_tm);
        date += buf;
    }

    return date;
}

void TConsole::BackupOldLog() {
    fs::path Path = "Server.log";
    if (fs::exists(Path)) {
        auto OldLog = Path.filename().stem().string() + ".old.log";
        try {
            fs::rename(Path, OldLog);
            beammp_debug("renamed old log file to '" + OldLog + "'");
        } catch (const std::exception& e) {
            beammp_warn(e.what());
        }
    }
}

void TConsole::StartLoggingToFile() {
    mLogFileStream.open("Server.log");
    Application::Console().Internal().on_write = [this](const std::string& ToWrite) {
        // TODO: Sanitize by removing all ansi escape codes (vt100)
        std::unique_lock Lock(mLogFileStreamMtx);
        mLogFileStream.write(ToWrite.c_str(), ToWrite.size());
        mLogFileStream.write("\n", 1);
        mLogFileStream.flush();
    };
}

void TConsole::ChangeToLuaConsole(const std::string& LuaStateId) {
    if (!mIsLuaConsole) {
        if (!mLuaEngine) {
            beammp_error("Lua engine not initialized yet, please wait and try again");
            return;
        }
        mLuaEngine->EnsureStateExists(mDefaultStateId, "Console");
        mStateId = LuaStateId;
        mIsLuaConsole = true;
        if (mStateId != mDefaultStateId) {
            Application::Console().WriteRaw("Attached to Lua state '" + mStateId + "'. For help, type `:help`. To detach, type `:exit`");
            mCommandline->set_prompt("lua @" + LuaStateId + "> ");
        } else {
            Application::Console().WriteRaw("Attached to Lua. For help, type `:help`. To detach, type `:exit`");
            mCommandline->set_prompt("lua> ");
        }
        mCachedRegularHistory = mCommandline->history();
        mCommandline->set_history(mCachedLuaHistory);
    }
}

void TConsole::ChangeToRegularConsole() {
    if (mIsLuaConsole) {
        mIsLuaConsole = false;
        if (mStateId != mDefaultStateId) {
            Application::Console().WriteRaw("Detached from Lua state '" + mStateId + "'.");
        } else {
            Application::Console().WriteRaw("Detached from Lua.");
        }
        mCachedLuaHistory = mCommandline->history();
        mCommandline->set_history(mCachedRegularHistory);
        mCommandline->set_prompt("> ");
        mStateId = mDefaultStateId;
    }
}

bool TConsole::EnsureArgsCount(const std::vector<std::string>& args, size_t n) {
    if (n == 0 && !args.empty()) {
        Application::Console().WriteRaw("This command expects no arguments.");
        return false;
    } else if (args.size() != n) {
        Application::Console().WriteRaw("Expected " + std::to_string(n) + " argument(s), instead got " + std::to_string(args.size()));
        return false;
    } else {
        return true;
    }
}

bool TConsole::EnsureArgsCount(const std::vector<std::string>& args, size_t min, size_t max) {
    if (min == max) {
        return EnsureArgsCount(args, min);
    } else {
        if (args.size() > max) {
            Application::Console().WriteRaw("Too many arguments. At most " + std::to_string(max) + " argument(s) expected, got " + std::to_string(args.size()) + " instead.");
            return false;
        } else if (args.size() < min) {
            Application::Console().WriteRaw("Too few arguments. At least " + std::to_string(min) + " argument(s) expected, got " + std::to_string(args.size()) + " instead.");
            return false;
        }
    }
    return true;
}

void TConsole::Command_Lua(const std::string&, const std::vector<std::string>& args) {
    if (!EnsureArgsCount(args, 0, 1)) {
        return;
    }
    if (args.size() == 1) {
        auto NewStateId = args.at(0);
        beammp_assert(!NewStateId.empty());
        if (mLuaEngine->HasState(NewStateId)) {
            ChangeToLuaConsole(NewStateId);
        } else {
            Application::Console().WriteRaw("Lua state '" + NewStateId + "' is not a known state. Didn't switch to Lua.");
        }
    } else if (args.empty()) {
        ChangeToLuaConsole(mDefaultStateId);
    }
}

void TConsole::Command_Help(const std::string&, const std::vector<std::string>& args) {
    if (!EnsureArgsCount(args, 0)) {
        return;
    }
    static constexpr const char* sHelpString = R"(
    Commands:
        help                       displays this help
        exit                       shuts down the server
        kick <name> [reason]       kicks specified player with an optional reason
        list                       lists all players and info about them
        say <message>              sends the message to all players in chat
        lua [state id]             switches to lua, optionally into a specific state id's lua
        settings [command]         sets or gets settings for the server, run `settings help` for more info
        status                     how the server is doing and what it's up to
        clear                      clears the console window
        version                    displays the server version
        protectmod <name> <value>  sets whether a mod is protected, value can be true or false
        reloadmods                 reloads all mods from the Resources Client folder)";
    Application::Console().WriteRaw("BeamMP-Server Console: " + std::string(sHelpString));
}

std::string TConsole::ConcatArgs(const std::vector<std::string>& args, char space) {
    std::string Result;
    for (const auto& arg : args) {
        Result += arg + space;
    }
    Result = Result.substr(0, Result.size() - 1); // strip trailing space
    return Result;
}

void TConsole::Command_Clear(const std::string&, const std::vector<std::string>& args) {
    if (!EnsureArgsCount(args, 0, size_t(-1))) {
        return;
    }
    mCommandline->write("\x1b[;H\x1b[2J");
}

void TConsole::Command_Version(const std::string& cmd, const std::vector<std::string>& args) {
    if (!EnsureArgsCount(args, 0)) {
        return;
    }

    std::string platform;
#if defined(BEAMMP_WINDOWS)
    platform = "Windows";
#elif defined(BEAMMP_LINUX)
    platform = "Linux";
#elif defined(BEAMMP_FREEBSD)
    platform = "FreeBSD";
#elif defined(BEAMMP_APPLE)
    platform = "Apple";
#else
    platform = "Unknown";
#endif

    Application::Console().WriteRaw("Platform: " + platform);
    Application::Console().WriteRaw("Server:   v" + Application::ServerVersionString());
    std::string lua_version = fmt::format("Lua:      v{}.{}.{}", LUA_VERSION_MAJOR, LUA_VERSION_MINOR, LUA_VERSION_RELEASE);
    Application::Console().WriteRaw(lua_version);
    std::string openssl_version = fmt::format("OpenSSL:  v{}.{}.{}", OPENSSL_VERSION_MAJOR, OPENSSL_VERSION_MINOR, OPENSSL_VERSION_PATCH);
    Application::Console().WriteRaw(openssl_version);
}
void TConsole::Command_ProtectMod(const std::string& cmd, const std::vector<std::string>& args) {
    if (!EnsureArgsCount(args, 2)) {
        return;
    }

    const auto& ModName = args.at(0);
    const auto& Protect = args.at(1);

    for (auto mod : mLuaEngine->Network().ResourceManager().GetMods()) {
        if (mod["file_name"].get<std::string>() == ModName) {
            mLuaEngine->Network().ResourceManager().SetProtected(ModName, Protect == "true");
            Application::Console().WriteRaw("Mod " + ModName + " is now " + (Protect == "true" ? "protected" : "unprotected"));
            return;
        }
    }

    Application::Console().WriteRaw("Mod " + ModName + " not found.");
}
void TConsole::Command_ReloadMods(const std::string& cmd, const std::vector<std::string>& args) {
    if (!EnsureArgsCount(args, 0)) {
        return;
    }

    mLuaEngine->Network().ResourceManager().RefreshFiles();
    Application::Console().WriteRaw("Mods reloaded.");
}

void TConsole::Command_NetTest(const std::string& cmd, const std::vector<std::string>& args) {
    unsigned int status = 0;

    std::string T = Http::GET(
    Application::GetServerCheckUrl() + "/api/v2/beammp/" + std::to_string(Application::Settings.getAsInt(Settings::Key::General_Port)), &status
        );

    beammp_debugf("Status and response from Server Check API: {0}, {1}", status, T);

    auto Doc = nlohmann::json::parse(T, nullptr, false);

    if (Doc.is_discarded() || !Doc.is_object()) {
        beammp_warn("Failed to parse Server Check API response, however the server will most likely still work correctly.");
    } else {
        std::string status = Doc["status"];
        std::string details = "Response from Server Check API: " + std::string(Doc["details"]);
        if (status == "ok") {
            beammp_info(details);
        } else {
            beammp_warn(details);
        }
    }
}

void TConsole::Command_Kick(const std::string&, const std::vector<std::string>& args) {
    if (!EnsureArgsCount(args, 1, size_t(-1))) {
        return;
    }
    auto Name = args.at(0);
    std::string Reason = "Kicked by server console";
    if (args.size() > 1) {
        Reason = ConcatArgs({ args.begin() + 1, args.end() });
    }
    beammp_trace("attempt to kick '" + Name + "' for '" + Reason + "'");
    auto Result = Application::Control().Execute("players.kick", {
        { "name", Name },
        { "reason", Reason },
        { "prefix_match", true },
    });
    if (!Result.at("ok").get<bool>()) {
        Application::Console().WriteRaw("Error: " + Result.at("error").get<std::string>());
        return;
    }
    Application::Console().WriteRaw("Kicked player '" + Result.at("data").at("player").at("name").get<std::string>() + "' for reason: '" + Reason + "'.");
}

std::tuple<std::string, std::vector<std::string>> TConsole::ParseCommand(const std::string& CommandWithArgs) {
    // Algorithm designed and implemented by Lion Kortlepel (c) 2022
    // It correctly splits arguments, including respecting single and double quotes, as well as backticks
    auto End_i = CommandWithArgs.find_first_of(' ');
    std::string Command = CommandWithArgs.substr(0, End_i);
    std::string ArgsStr {};
    if (End_i != std::string::npos) {
        ArgsStr = CommandWithArgs.substr(End_i);
    }
    std::vector<std::string> Args;
    char* PrevPtr = ArgsStr.data();
    char* Ptr = ArgsStr.data();
    const char* End = ArgsStr.data() + ArgsStr.size();
    while (Ptr != End) {
        std::string Arg = "";
        // advance while space
        while (Ptr != End && std::isspace(*Ptr))
            ++Ptr;
        PrevPtr = Ptr;
        // advance while NOT space, also handle quotes
        while (Ptr != End && !std::isspace(*Ptr)) {
            // TODO: backslash escaping quotes
            for (char Quote : { '"', '\'', '`' }) {
                if (*Ptr == Quote) {
                    // seek if there's a closing quote
                    // if there is, go there and continue, otherwise ignore
                    char* Seeker = Ptr + 1;
                    while (Seeker != End && *Seeker != Quote)
                        ++Seeker;
                    if (Seeker != End) {
                        // found closing quote
                        Ptr = Seeker;
                    }
                    break; // exit for loop
                }
            }
            ++Ptr;
        }
        // this is required, otherwise we get negative int to unsigned cast in the next operations
        beammp_assert(PrevPtr <= Ptr);
        Arg = std::string(PrevPtr, std::string::size_type(Ptr - PrevPtr));
        // remove quotes if enclosed in quotes
        for (char Quote : { '"', '\'', '`' }) {
            if (!Arg.empty() && Arg.at(0) == Quote && Arg.at(Arg.size() - 1) == Quote) {
                Arg = Arg.substr(1, Arg.size() - 2);
                break;
            }
        }
        if (!Arg.empty()) {
            Args.push_back(Arg);
        }
    }
    return { Command, Args };
}

template <class... Ts>
struct overloaded : Ts... {
    using Ts::operator()...;
};
template <class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

void TConsole::Command_Settings(const std::string&, const std::vector<std::string>& args) {

    static constexpr const char* sHelpString = R"(
    Settings:
        settings help                               displays this help
        settings list                               lists all settings
        settings get <category> <setting>           prints current value of specified setting
        settings set <category> <setting> <value>   sets specified setting to value
        )";

    if (args.empty()) {
        beammp_errorf("No arguments specified for command 'settings'!");
        Application::Console().WriteRaw("BeamMP-Server Console: " + std::string(sHelpString));
        return;
    }

    if (args.front() == "help") {

        Application::Console().WriteRaw("BeamMP-Server Console: " + std::string(sHelpString));
        return;
    } else if (args.front() == "get") {
        if (args.size() < 3) {
            beammp_errorf("'settings get' needs at least two arguments!");

            Application::Console().WriteRaw("BeamMP-Server Console: " + std::string(sHelpString));
            return;
        }

        try {
            Settings::SettingsAccessControl acl = Application::Settings.getConsoleInputAccessMapping(ComposedKey { args.at(1), args.at(2) });
            Settings::SettingsTypeVariant keyType = Application::Settings.get(acl.first);

            std::visit(
                overloaded {
                    [&args](std::string keyValue) {
                        Application::Console().WriteRaw(fmt::format("'{}::{}' = {}", args.at(1), args.at(2), keyValue));
                    },
                    [&args](int keyValue) {
                        Application::Console().WriteRaw(fmt::format("'{}::{}' = {}", args.at(1), args.at(2), keyValue));
                    },
                    [&args](bool keyValue) {
                        Application::Console().WriteRaw(fmt::format("'{}::{}' = {}", args.at(1), args.at(2), keyValue));
                    }

                },
                keyType);

        } catch (std::logic_error& e) {
            beammp_errorf("Error when getting key: {}", e.what());
            return;
        }
    } else if (args.front() == "set") {
        if (args.size() <= 3) {
            beammp_errorf("'settings set' needs at least three arguments!");

            Application::Console().WriteRaw("BeamMP-Server Console: " + std::string(sHelpString));
            return;
        }

        try {

            Settings::SettingsAccessControl acl = Application::Settings.getConsoleInputAccessMapping(ComposedKey { args.at(1), args.at(2) });
            Settings::SettingsTypeVariant keyType = Application::Settings.get(acl.first);

            std::visit(
                overloaded {
                    [&args](std::string keyValue) {
                        Application::Settings.setConsoleInputAccessMapping(ComposedKey { args.at(1), args.at(2) }, std::string(args.at(3)));
                        Application::Console().WriteRaw(fmt::format("{}::{} := {}", args.at(1), args.at(2), std::string(args.at(3))));
                    },
                    [&args](int keyValue) {
                        Application::Settings.setConsoleInputAccessMapping(ComposedKey { args.at(1), args.at(2) }, std::stoi(args.at(3)));
                        Application::Console().WriteRaw(fmt::format("{}::{} := {}", args.at(1), args.at(2), std::stoi(args.at(3))));
                    },
                    [&args](bool keyValue) {
                        if (args.at(3) == "true") {
                            Application::Settings.setConsoleInputAccessMapping(ComposedKey { args.at(1), args.at(2) }, true);
                            Application::Console().WriteRaw(fmt::format("{}::{} := {}", args.at(1), args.at(2), "true"));
                        } else if (args.at(3) == "false") {
                            Application::Settings.setConsoleInputAccessMapping(ComposedKey { args.at(1), args.at(2) }, false);
                            Application::Console().WriteRaw(fmt::format("{}::{} := {}", args.at(1), args.at(2), "false"));
                        } else {
                            beammp_errorf("Error when setting key: {}::{} : Unknown literal, use either 'true', or 'false' to set boolean values.", args.at(1), args.at(2));
                        }
                    }

                },
                keyType);

        } catch (std::logic_error& e) {
            beammp_errorf("Exception when setting settings key via console: {}", e.what());
            return;
        }

    } else if (args.front() == "list") {
        for (const auto& [composedKey, keyACL] : Application::Settings.getAccessControlMap()) {
            // even though we have the value, we want to ignore it in order to make use of access
            // control checks

            if (keyACL.second != Settings::SettingsAccessMask::NO_ACCESS) {

                try {

                    Settings::SettingsAccessControl acl = Application::Settings.getConsoleInputAccessMapping(composedKey);
                    Settings::SettingsTypeVariant keyType = Application::Settings.get(acl.first);

                    std::visit(
                        overloaded {
                            [&composedKey](std::string keyValue) {
                                Application::Console().WriteRaw(fmt::format("{} = {}", composedKey, keyValue));
                            },
                            [&composedKey](int keyValue) {
                                Application::Console().WriteRaw(fmt::format("{} = {}", composedKey, keyValue));
                            },
                            [&composedKey](bool keyValue) {
                                Application::Console().WriteRaw(fmt::format("{} = {}", composedKey, keyValue));
                            }

                        },
                        keyType);
                } catch (std::logic_error& e) {
                    beammp_errorf("Error when getting key: {}", e.what());
                }
            }
        }
    } else {
        beammp_errorf("Unknown argument for command 'settings': {}", args.front());

        Application::Console().WriteRaw("BeamMP-Server Console: " + std::string(sHelpString));
        return;
    }
}

void TConsole::Command_Say(const std::string& FullCmd) {
    if (FullCmd.size() > 3) {
        auto Message = FullCmd.substr(4);
        auto Result = Application::Control().Execute("chat.send", {
            { "target_id", -1 },
            { "message", Message },
        });
        if (!Result.at("ok").get<bool>()) {
            Application::Console().WriteRaw("Error: " + Result.at("error").get<std::string>());
        } else if (!Application::Settings.getAsBool(Settings::Key::General_LogChat)) {
            Application::Console().WriteRaw("Chat message sent!");
        }
    }
}

void TConsole::Command_List(const std::string&, const std::vector<std::string>& args) {
    if (!EnsureArgsCount(args, 0)) {
        return;
    }
    auto Result = Application::Control().Execute("players.list");
    if (!Result.at("ok").get<bool>()) {
        Application::Console().WriteRaw("Error: " + Result.at("error").get<std::string>());
        return;
    }
    const auto& Players = Result.at("data").at("players");
    if (Players.empty()) {
        Application::Console().WriteRaw("No players online.");
    } else {
        std::stringstream ss;
        ss << std::left << std::setw(25) << "Name" << std::setw(6) << "ID" << std::setw(6) << "Cars" << std::endl;
        for (const auto& Player : Players) {
            ss << std::left << std::setw(25) << Player.at("name").get<std::string>()
               << std::setw(6) << Player.at("id").get<int>()
               << std::setw(6) << Player.at("cars").get<int>() << "\n";
        }
        auto Str = ss.str();
        Application::Console().WriteRaw(Str.substr(0, Str.size() - 1));
    }
}

void TConsole::Command_Status(const std::string&, const std::vector<std::string>& args) {
    if (!EnsureArgsCount(args, 0)) {
        return;
    }
    auto Result = Application::Control().Execute("server.status");
    if (!Result.at("ok").get<bool>()) {
        Application::Console().WriteRaw("Error: " + Result.at("error").get<std::string>());
        return;
    }

    const auto& Data = Result.at("data");
    const auto& Players = Data.at("players");
    const auto& Lua = Data.at("lua");
    const auto& Subsystems = Data.at("subsystems");
    const auto& Counts = Subsystems.at("counts");
    std::stringstream Status;

    auto JoinSubsystems = [](const nlohmann::json& Systems, const std::string& Name) {
        std::string Out;
        for (const auto& System : Systems) {
            if (System.at("status").get<std::string>() == Name) {
                if (!Out.empty()) {
                    Out += ", ";
                }
                Out += System.at("name").get<std::string>();
            }
        }
        return Out;
    };

    Status << "BeamMP-Server Status:\n"
           << "\tTotal Players:             " << Players.at("total").get<size_t>() << "\n"
           << "\tSyncing Players:           " << Players.at("syncing").get<size_t>() << "\n"
           << "\tSynced Players:            " << Players.at("synced").get<size_t>() << "\n"
           << "\tConnected Players:         " << Players.at("udp_connected").get<size_t>() << "\n"
           << "\tGuests:                    " << Players.at("guests").get<size_t>() << "\n"
           << "\tCars:                      " << Data.at("cars").get<size_t>() << "\n"
           << "\tUptime:                    " << Data.at("uptime_ms").get<size_t>() << "ms (~" << size_t(double(Data.at("uptime_ms").get<size_t>()) / 1000.0 / 60.0 / 60.0) << "h) \n"
           << "\tLua:\n"
           << "\t\tQueued results to check:     " << Lua.at("queued_results_to_check").get<size_t>() << "\n"
           << "\t\tStates:                      " << Lua.at("states").get<size_t>() << "\n"
           << "\t\tEvent timers:                " << Lua.at("event_timers").get<size_t>() << "\n"
           << "\t\tEvent handlers:              " << Lua.at("event_handlers").get<size_t>() << "\n"
           << "\tSubsystems:\n"
           << "\t\tGood/Starting/Bad:           " << Counts.at("good").get<size_t>() << "/" << Counts.at("starting").get<size_t>() << "/" << Counts.at("bad").get<size_t>() << "\n"
           << "\t\tShutting down/Shut down:     " << Counts.at("shutting_down").get<size_t>() << "/" << Counts.at("shutdown").get<size_t>() << "\n"
           << "\t\tGood:                        [ " << JoinSubsystems(Subsystems.at("systems"), "good") << " ]\n"
           << "\t\tStarting:                    [ " << JoinSubsystems(Subsystems.at("systems"), "starting") << " ]\n"
           << "\t\tBad:                         [ " << JoinSubsystems(Subsystems.at("systems"), "bad") << " ]\n"
           << "\t\tShutting down:               [ " << JoinSubsystems(Subsystems.at("systems"), "shutting_down") << " ]\n"
           << "\t\tShut down:                   [ " << JoinSubsystems(Subsystems.at("systems"), "shutdown") << " ]\n"
           << "";

    Application::Console().WriteRaw(Status.str());
}

void TConsole::RunAsCommand(const std::string& cmd, bool IgnoreNotACommand) {
    auto FutureIsNonNil =
        [](const std::shared_ptr<TLuaResult>& Future) {
            if (!Future->Error && Future->Result.valid()) {
                auto Type = Future->Result.get_type();
                return Type != sol::type::lua_nil && Type != sol::type::none;
            }
            return false;
        };
    std::vector<std::shared_ptr<TLuaResult>> NonNilFutures;
    { // Futures scope
        auto Futures = mLuaEngine->TriggerEvent("onConsoleInput", "", cmd);
        TLuaEngine::WaitForAll(Futures, std::chrono::seconds(5));
        size_t Count = 0;
        for (auto& Future : Futures) {
            if (!Future->Error) {
                ++Count;
            }
        }
        for (const auto& Future : Futures) {
            if (FutureIsNonNil(Future)) {
                NonNilFutures.push_back(Future);
            }
        }
    }
    if (NonNilFutures.empty()) {
        if (!IgnoreNotACommand) {
            Application::Console().WriteRaw("Error: Unknown command: '" + cmd + "'. Type 'help' to see a list of valid commands.");
        }
    } else {
        std::stringstream Reply;
        if (NonNilFutures.size() > 1) {
            for (size_t i = 0; i < NonNilFutures.size(); ++i) {
                Reply << NonNilFutures[i]->StateId << ": \n"
                      << LuaAPI::LuaToString(NonNilFutures[i]->Result);
                if (i < NonNilFutures.size() - 1) {
                    Reply << "\n";
                }
            }
        } else {
            Reply << LuaAPI::LuaToString(NonNilFutures[0]->Result);
        }
        Application::Console().WriteRaw(Reply.str());
    }
}

void TConsole::HandleLuaInternalCommand(const std::string& cmd) {
    if (cmd == "exit") {
        ChangeToRegularConsole();
    } else if (cmd == "queued") {
        auto QueuedFunctions = LuaAPI::MP::Engine->Debug_GetStateFunctionQueueForState(mStateId);
        Application::Console().WriteRaw("Pending functions in State '" + mStateId + "'");
        std::unordered_map<std::string, size_t> FunctionsCount;
        std::vector<std::string> FunctionsInOrder;
        while (!QueuedFunctions.empty()) {
            auto Tuple = QueuedFunctions.front();
            QueuedFunctions.erase(QueuedFunctions.begin());
            FunctionsInOrder.push_back(Tuple.FunctionName);
            FunctionsCount[Tuple.FunctionName] += 1;
        }
        std::set<std::string> Uniques;
        for (const auto& Function : FunctionsInOrder) {
            if (Uniques.count(Function) == 0) {
                Uniques.insert(Function);
                if (FunctionsCount.at(Function) > 1) {
                    Application::Console().WriteRaw("    " + Function + " (" + std::to_string(FunctionsCount.at(Function)) + "x)");
                } else {
                    Application::Console().WriteRaw("    " + Function);
                }
            }
        }
        Application::Console().WriteRaw("Executed functions waiting to be checked in State '" + mStateId + "'");
        for (const auto& Function : LuaAPI::MP::Engine->Debug_GetResultsToCheckForState(mStateId)) {
            Application::Console().WriteRaw("    '" + Function.Function + "' (Ready? " + (Function.Ready ? "Yes" : "No") + ", Error? " + (Function.Error ? "Yes: '" + Function.ErrorMessage + "'" : "No") + ")");
        }
    } else if (cmd == "events") {
        auto Events = LuaAPI::MP::Engine->Debug_GetEventsForState(mStateId);
        Application::Console().WriteRaw("Registered Events + Handlers for State '" + mStateId + "'");
        for (const auto& EventHandlerPair : Events) {
            Application::Console().WriteRaw("    Event '" + EventHandlerPair.first + "'");
            for (const auto& Handler : EventHandlerPair.second) {
                Application::Console().WriteRaw("        " + Handler);
            }
        }
    } else if (cmd == "help") {
        Application::Console().WriteRaw(R"(BeamMP Lua Debugger
    All commands must be prefixed with a `:`. Non-prefixed commands are interpreted as Lua.

Commands
    :exit         detaches (exits) from this Lua console
    :help         displays this help
    :events       shows a list of currently registered events
    :queued       shows a list of all pending and queued functions)");
    } else {
        beammp_error("internal command '" + cmd + "' is not known");
    }
}

TConsole::TConsole() {
}

void TConsole::InitializeCommandline() {
    mCommandline = std::make_unique<Commandline>();
    mCommandline->enable_history();
    mCommandline->set_history_limit(20);
    mCommandline->set_prompt("> ");
    BackupOldLog();
    mCommandline->on_command = [this](Commandline& c) {
        try {
            auto TrimmedCmd = c.get_command();
            TrimmedCmd = TrimString(TrimmedCmd);
            auto [cmd, args] = ParseCommand(TrimmedCmd);
            mCommandline->write(mCommandline->prompt() + TrimmedCmd);
            if (mIsLuaConsole) {
                if (!mLuaEngine) {
                    beammp_info("Lua not started yet, please try again in a second");
                } else if (!cmd.empty() && cmd.at(0) == ':') {
                    HandleLuaInternalCommand(cmd.substr(1));
                } else {
                    auto Future = mLuaEngine->EnqueueScript(mStateId, { std::make_shared<std::string>(TrimmedCmd), "", "" });
                    Future->WaitUntilReady();
                    if (Future->Error) {
                        beammp_lua_error("error in " + mStateId + ": " + Future->ErrorMessage);
                    }
                }
            } else {
                if (!mLuaEngine) {
                    beammp_error("Attempted to run a command before Lua engine started. Please wait and try again.");
                } else if (cmd == "exit") {
                    beammp_info("gracefully shutting down");
                    Application::GracefullyShutdown();
                } else if (cmd == "say") {
                    RunAsCommand(TrimmedCmd, true);
                    Command_Say(TrimmedCmd);
                } else {
                    if (mCommandMap.find(cmd) != mCommandMap.end()) {
                        mCommandMap.at(cmd)(cmd, args);
                        RunAsCommand(TrimmedCmd, true);
                    } else {
                        RunAsCommand(TrimmedCmd);
                    }
                }
            }
        } catch (const std::exception& e) {
            beammp_error("Console died with: " + std::string(e.what()) + ". This could be a fatal error and could cause the server to terminate.");
        }
    };
    mCommandline->on_autocomplete = [this](Commandline&, std::string stub, int) {
        std::vector<std::string> suggestions;
        try {
            if (mIsLuaConsole) { // if lua
                if (!mLuaEngine) {
                    beammp_info("Lua not started yet, please try again in a second");
                } else {
                    std::string prefix {}; // stores non-table part of input
                    for (size_t i = stub.length(); i > 0; i--) { // separate table from input
                        if (!std::isalnum(stub[i - 1]) && stub[i - 1] != '_' && stub[i - 1] != '.') {
                            prefix = stub.substr(0, i);
                            stub = stub.substr(i);
                            break;
                        }
                    }

                    // turn string into vector of keys
                    std::vector<std::string> tablekeys;

                    SplitString(stub, '.', tablekeys);

                    // remove last key if incomplete
                    if (stub.rfind('.') != stub.size() - 1 && !tablekeys.empty()) {
                        tablekeys.pop_back();
                    }

                    auto keys = mLuaEngine->GetStateTableKeysForState(mStateId, tablekeys);

                    for (const auto& key : keys) { // go through each bottom-level key
                        auto last_dot = stub.rfind('.');
                        std::string last_atom;
                        if (last_dot != std::string::npos) {
                            last_atom = stub.substr(last_dot + 1);
                        }
                        std::string before_last_atom = stub.substr(0, last_dot + 1); // get last confirmed key
                        auto last = stub.substr(stub.rfind('.') + 1);
                        std::string::size_type n = key.find(last);
                        if (n == 0) {
                            suggestions.push_back(prefix + before_last_atom + key);
                        }
                    }
                }
            } else { // if not lua
                if (stub.find("lua") == 0) { // starts with "lua" means we should suggest state names
                    std::string after_prefix = TrimString(stub.substr(3));
                    auto stateNames = mLuaEngine->GetLuaStateNames();

                    for (const auto& name : stateNames) {
                        if (name.find(after_prefix) == 0) {
                            suggestions.push_back("lua " + name);
                        }
                    }
                } else {
                    for (const auto& [cmd_name, cmd_fn] : mCommandMap) {
                        if (cmd_name.find(stub) == 0) {
                            suggestions.push_back(cmd_name);
                        }
                    }
                }
            }
        } catch (const std::exception& e) {
            beammp_error("Console died with: " + std::string(e.what()) + ". This could be a fatal error and could cause the server to terminate.");
        }
        std::sort(suggestions.begin(), suggestions.end());
        return suggestions;
    };
}

void TConsole::Write(const std::string& str) {
    auto ToWrite = GetDate() + str;
    {
        std::unique_lock Lock(mRecentLogMutex);
        mRecentLogEntries.push_back({ mNextLogSequence++, UnixTimestampMsNow(), ToWrite });
        while (mRecentLogEntries.size() > mMaxRecentLogEntries) {
            mRecentLogEntries.pop_front();
        }
    }
    // allows writing to stdout without an initialized console
    if (mCommandline) {
        mCommandline->write(ToWrite);
    } else {
        std::cout << ToWrite << std::endl;
    }
}

void TConsole::WriteRaw(const std::string& str) {
    {
        std::unique_lock Lock(mRecentLogMutex);
        mRecentLogEntries.push_back({ mNextLogSequence++, UnixTimestampMsNow(), str });
        while (mRecentLogEntries.size() > mMaxRecentLogEntries) {
            mRecentLogEntries.pop_front();
        }
    }
    // allows writing to stdout without an initialized console
    if (mCommandline) {
        mCommandline->write(str);
    } else {
        std::cout << str << std::endl;
    }
}

void TConsole::RecordEvent(std::string Type, std::string Category, nlohmann::json Data) {
    std::unique_lock Lock(mRecentEventMutex);
    mRecentEventEntries.push_back({ mNextEventSequence++, UnixTimestampMsNow(), std::move(Type), std::move(Category), std::move(Data) });
    while (mRecentEventEntries.size() > mMaxRecentEventEntries) {
        mRecentEventEntries.pop_front();
    }
}

std::vector<TConsole::TLogEntry> TConsole::RecentLogEntries(size_t Limit, std::optional<uint64_t> AfterSequence) const {
    std::vector<TLogEntry> Entries;
    std::unique_lock Lock(mRecentLogMutex);
    Entries.reserve(std::min(Limit, mRecentLogEntries.size()));
    for (const auto& Entry : mRecentLogEntries) {
        if (AfterSequence.has_value() && Entry.Sequence <= *AfterSequence) {
            continue;
        }
        Entries.push_back(Entry);
        if (Entries.size() >= Limit) {
            break;
        }
    }
    return Entries;
}

std::vector<TConsole::TEventEntry> TConsole::RecentEventEntries(size_t Limit, std::optional<uint64_t> AfterSequence) const {
    std::vector<TEventEntry> Entries;
    std::unique_lock Lock(mRecentEventMutex);
    Entries.reserve(std::min(Limit, mRecentEventEntries.size()));
    for (const auto& Entry : mRecentEventEntries) {
        if (AfterSequence.has_value() && Entry.Sequence <= *AfterSequence) {
            continue;
        }
        Entries.push_back(Entry);
        if (Entries.size() >= Limit) {
            break;
        }
    }
    return Entries;
}

uint64_t TConsole::LatestLogSequence() const {
    std::unique_lock Lock(mRecentLogMutex);
    return mRecentLogEntries.empty() ? 0 : mRecentLogEntries.back().Sequence;
}

uint64_t TConsole::LatestEventSequence() const {
    std::unique_lock Lock(mRecentEventMutex);
    return mRecentEventEntries.empty() ? 0 : mRecentEventEntries.back().Sequence;
}

void TConsole::InitializeLuaConsole(TLuaEngine& Engine) {
    mLuaEngine = &Engine;
}
