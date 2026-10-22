# minor-bon: startup settings and name-log bugs (fixed)

Reported: 2026-10-22. Fixed and verified locally on `minor-bon`: 2026-10-22.

**Status: fixed on `minor-bon`.** The full Windows x64 Release server and test
executable have been built successfully. Rebuild from a revision containing these
fixes, or use a release containing them, to update an older server binary.

## Implemented fix

- Moved startup override handling to `TConfig::ApplyStartupSettingOverride()`.
  Both startup overrides and configuration loading now use the same registry of
  all 24 supported General, Misc, HttpApi, and SpatialRebase settings.
- Unknown setting names, malformed assignments, and invalid boolean/integer
  values are rejected with an error and exit code 1. Success is logged only after
  validation and successful delivery to the environment variable used by the loader.
- Overrides take precedence over existing environment variables and TOML values.
  Repeated overrides retain command-line order, with the last value winning.
  Values containing spaces or additional `=` characters are preserved.
- Provider-specific IP and port environment mappings are respected. The dedicated
  `--port` flag still takes priority over `--setting=General.Port=...`.
- The startup name log now reads `Application::Settings`, displaying
  `Server name: <active name>`.
- Corrected the `General.IP` and `Misc.UpdateReminderTime` built-in defaults to
  be strings rather than accidentally selecting the boolean variant.

## Server-side verification

The earlier vcpkg lock has cleared, and the required dependencies are installed.
The full project was configured with Visual Studio 2022 and built for Windows
x64 Release, including the normal test target. The local CMake cache explicitly
selects the Release `lua.lib` to avoid mixing debug and release runtime libraries.

Build outputs:

- `out/build/minor-bon/Release/BeamMP-Server.exe`
- `out/build/minor-bon/Release/BeamMP-Server-tests.exe`

Verification results:

- **39 test cases and 44,339 assertions passed** in the full test executable,
  including the IPv4 authentication follow-up below.
- Added regression coverage in `src/TConfig.cpp` for all 24 startup settings,
  boolean `false`, integers, strings containing spaces and `=`, repeated
  overrides, unknown/malformed keys, invalid typed values, environment/TOML
  precedence, config-free loading without creating a file, and provider IP/port
  mappings.
- The built server's `--version` smoke check succeeds and prints
  `BeamMP-Server v3.9.3`.
- Binary smoke checks confirm unknown keys and invalid integer values print an
  error and exit with code 1 before server initialization.
- An isolated server bound to `0.0.0.0` accepted a native IPv4 connection and
  returned the successful version-handshake response (`A`).

This verifies the full build, configuration regression suite, and early CLI
behavior. A live multiplayer session and backend authentication were not tested.

## Related follow-up: IPv4 connection closed before authentication (fixed)

After the startup-setting fix, `General.IP=0.0.0.0` correctly selects an IPv4
listener. This exposed an existing bug in `TNetwork::Authentication()`, which
unconditionally cast the remote address to IPv6. Native IPv4 connections threw
`bad address cast` and were closed before the version handshake. The observed
client message was `Disconnected: Socket Closed Code 3`, with this server log:

```text
Error during handling of code C - client left in invalid state, closing socket: bad address cast
```

Authentication now checks the address family before casting. Native IPv4 and
native IPv6 addresses are preserved; IPv4-mapped IPv6 addresses are normalized to
IPv4 for client identifiers and backend authentication. The diagnostic log also
uses the normalized address without an unsafe cast.

Regression tests cover native IPv4 (including loopback), mapped IPv4, and native
IPv6. The rebuilt Release binary also passed the native IPv4 version-handshake
smoke check described above. Deploy the rebuilt executable and restart the server
to apply this follow-up fix to an already running instance.

## Original report

The findings below describe the affected version of the local `origin/minor-bon`
ref before this fix.

### 1. `--setting` reports success for settings it never applies

In `src/main.cpp`, `ApplyStartupSettingOverride()` only applies a value when
`EnvVarForStartupSetting()` returns an environment-variable mapping. If no mapping
exists, it still returns success and logs `Startup override applied: Section.Key`.

The mapping currently covers only these General settings: `AuthKey`, `Name`,
`Map`, `ResourceFolder`, and `AllowGuests` (plus HttpApi and SpatialRebase settings).
However, `src/TConfig.cpp` already supports environment variables for the other
General settings and the Misc settings.

Consequently, these valid overrides are silently ignored:

- `General.Description`, `General.Private`, `General.Tags`
- `General.MaxCars`, `General.MaxPlayers`, `General.IP`, `General.Port`
- `General.Debug`, `General.LogChat`, `General.InformationPacket`
- `Misc.ImScaredOfUpdates`, `Misc.UpdateReminderTime`

With `--no-config`, ignored settings retain built-in defaults unless supplied
through the environment. With TOML enabled, they can retain TOML/environment values.
Unknown setting names also incorrectly report success.

#### Reproduction (before the fix)

Using a valid auth key, run:

```text
BeamMP-Server --no-config --setting=General.AuthKey=<valid-key> --setting=General.Description=CustomDescription --setting=General.Private=false --setting=General.MaxPlayers=23
```

All overrides are logged as applied, but description, listing state, and player
limit are not changed by those arguments. Supplying `BEAMMP_DESCRIPTION`,
`BEAMMP_PRIVATE`, and `BEAMMP_MAX_PLAYERS` instead reaches the existing TConfig loader.
For the game port, the dedicated `--port=12345` flag works.

#### Original suggested fix

Complete the mapping for all supported startup settings, or use a shared typed
settings registry so CLI and configuration loading cannot drift. Reject unknown
keys and report success only after an override has actually been applied.

Add regression coverage for string, boolean (including `false`), and integer
overrides; repeated overrides; values containing spaces or `=`; unknown keys;
and precedence with environment variables, TOML, and `--port`.

### 2. Server-name startup log reads defaults instead of active settings

In `src/main.cpp`, this code constructs a separate default settings object:

```cpp
Settings settings {};
beammp_infof("Server name set in new impl: {}", settings.getAsString(Settings::Key::General_Name));
```

The message can therefore print the default name even when `General.Name` was
successfully applied to `Application::Settings`. Read the active singleton for
this diagnostic, or remove the redundant message.

## Manager-side workaround for older binaries

The workaround below remains relevant to binaries built before this server-side
fix. Rebuilt binaries containing the fix accept the supported `--setting`
overrides directly.

GeoData2DDD's BonBeamMPServerManager now also passes supported settings through
their TConfig environment-variable equivalents, preserving trailing raw
`--setting` override order. This lets the existing minor-bon binary receive the
missing settings without a server rebuild.

Verification on the manager side: 35 relevant tests passed, including a real
child-process fixture checking delivery of name, description, private/public
state, limits, tags, logging flags, bind IP, and information-packet settings.
This verifies process delivery; it is not an end-to-end test of the server binary.
