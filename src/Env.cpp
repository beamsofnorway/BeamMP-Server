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

#include "Env.h"
#include <optional>

#ifdef BEAMMP_WINDOWS
#include <cstdlib>
#else
#include <cstdlib>
#endif

std::optional<std::string> Env::Get(Env::Key key) {
    auto StrKey = ToString(key);
    auto Value = std::getenv(StrKey.data());
    if (!Value || std::string_view(Value).empty()) {
        return std::nullopt;
    }
    return Value;
}

bool Env::Set(Env::Key key, std::string_view value) {
    auto StrKey = ToString(key);
    return Set(StrKey, value);
}

bool Env::Set(std::string_view key, std::string_view value) {
#ifdef _WIN32
    return _putenv_s(std::string(key).c_str(), std::string(value).c_str()) == 0;
#else
    return setenv(std::string(key).c_str(), std::string(value).c_str(), 1) == 0;
#endif
}

std::string_view Env::ToString(Env::Key key) {
    switch (key) {
    case Key::PROVIDER_UPDATE_MESSAGE:
        return "BEAMMP_PROVIDER_UPDATE_MESSAGE";
        break;
    case Key::PROVIDER_DISABLE_CONFIG:
        return "BEAMMP_PROVIDER_DISABLE_CONFIG";
        break;
    case Key::PROVIDER_DISABLE_MP_SET:
        return "BEAMMP_PROVIDER_DISABLE_MP_SET";
        break;
    case Key::PROVIDER_PORT_ENV:
        return "BEAMMP_PROVIDER_PORT_ENV";
        break;
    case Key::PROVIDER_IP_ENV:
        return "BEAMMP_PROVIDER_IP_ENV";
        break;
    case Key::HTTP_API_ENABLED:
        return "BEAMMP_HTTP_API_ENABLED";
        break;
    case Key::HTTP_API_HOST:
        return "BEAMMP_HTTP_API_HOST";
        break;
    case Key::HTTP_API_PORT:
        return "BEAMMP_HTTP_API_PORT";
        break;
    case Key::HTTP_API_TOKEN:
        return "BEAMMP_HTTP_API_TOKEN";
        break;
    }
    return "";
}
