-- Smoke check for the wowlib Lua module (run by ctest with LUA_CPATH set).
-- Names are snake_case throughout — welder's Lua naming convention reshapes
-- types, members and enumerators alike (ClientVersion -> client_version,
-- StorageKind.Mpq -> storage_kind.mpq, Locale.enUS -> locale.en_us) — and the
-- C++ namespaces arrive as submodules (wowlib.fs, wowlib.versions).

local wowlib = require("wowlib")

local function check(condition, message)
  if not condition then
    print("FAIL: " .. message)
    os.exit(1)
  end
end

-- version constants and enums; storage_kind is a read-only property (dot access)
check(wowlib.versions.wotlk.build == 12340, "wotlk build")
check(wowlib.versions.tww.build == 65299, "tww build")
check(wowlib.versions.wotlk.storage_kind == wowlib.storage_kind.mpq, "kind mpq")
check(wowlib.versions.shadowlands.storage_kind == wowlib.storage_kind.casc,
      "kind casc")
check(not pcall(function()
  wowlib.versions.wotlk.storage_kind = wowlib.storage_kind.casc
end), "storage_kind should be read-only")
check(wowlib.locale.en_us ~= nil, "locale enum")

-- FileDataID: the one identity helper the facade API needs
check(wowlib.file_data_id(775971).value == 775971, "fdid value")

-- FileKey: the generic file identity version-independent tools operate on
local key = wowlib.file_key("World/Maps/Azeroth/Azeroth.wdt")
check(key.path == "world\\maps\\azeroth\\azeroth.wdt", "path canonicalized")
key = wowlib.file_key(wowlib.file_data_id(775971))
check(key.fdid.value == 775971, "id-only key")

-- the surface stays minimal: internals are not welded
check(wowlib.error == nil and wowlib.error_code == nil, "error types not welded")
check(wowlib.fs.project_directory == nil, "project_directory should not be welded")
check(wowlib.fs.csv_listfile == nil, "csv_listfile should not be welded")

-- formats: versioned classes (snake_case reshaped), wire structs, flag enums
-- and the chunked-entity read/write methods
check(wowlib.formats.wmo_wotlk ~= nil, "wmo_wotlk welded")
check(wowlib.formats.wmo_the_war_within ~= nil, "wmo_the_war_within welded")
check(wowlib.formats.wmo_root_wotlk ~= nil, "wmo_root_wotlk welded")
check(wowlib.formats.wmo.smo_header ~= nil, "smo_header welded")
check(wowlib.formats.wmo.group_flags.exterior == 0x8, "flag enum bit")
local block = wowlib.formats.string_block()
check(block:add("textures/a.blp") == 0 and block:at(0) == "textures/a.blp",
      "string_block add/at")
local fresh_root = wowlib.formats.wmo_root_wotlk()
check(fresh_root.mver == 17, "fresh root MVER 17")
check(type(fresh_root:write()) == "string", "entity write method returns bytes")

-- settings are an immutable value: the synthesized field constructor exposes
-- one arity per omissible NSDMI-suffix tail; fields are read-only
local settings = wowlib.fs.file_system_settings("/no/such/client",
                                                wowlib.versions.wotlk)
check(settings.casc_product == "wow", "NSDMI default carried")
check(settings.custom_fdid_start.value == 1000000000, "fdid start default")
check(not pcall(function() settings.client_path = "/elsewhere" end),
      "settings fields should be read-only")

-- error translation: a failed Result raises a lua error carrying the code
local ok, err = pcall(function() return wowlib.fs.file_system.open(settings) end)
check(not ok, "open should have raised")
check(tostring(err):find("StorageOpenFailed") ~= nil,
      "error message carries the code: " .. tostring(err))

-- real-client round-trip (opt-in via env)
local clients = os.getenv("WOWLIB_TEST_CLIENTS_DIR")
if clients then
  settings = wowlib.fs.file_system_settings(
    clients .. "/World of Warcraft 3.3.5a", wowlib.versions.wotlk)
  local fs = wowlib.fs.file_system.open(settings)
  check(fs.kind == wowlib.storage_kind.mpq, "facade kind property")
  local data = fs:read_file("DBFilesClient/Map.dbc")
  check(type(data) == "string" and data:sub(1, 4) == "WDBC", "Map.dbc magic via Lua")

  -- the generic FileKey identity works across the facade
  check(fs:read_file(wowlib.file_key("DBFilesClient/Map.dbc")) == data,
        "same bytes via file_key")
  local resolved = fs:resolve(wowlib.file_key("DBFilesClient/Map.dbc"))
  check(resolved.path == "dbfilesclient\\map.dbc", "resolve canonicalizes")
  check(resolved.fdid == nil, "MPQ era: resolve leaves fdid absent")

  -- explicit close (welded from the protected C++ surface): deterministic
  -- release, StorageNotOpen afterwards, idempotent
  check(fs.is_open, "open filesystem reports is_open")
  fs:close()
  check(not fs.is_open, "closed filesystem reports not is_open")
  check(fs.kind == wowlib.storage_kind.mpq, "kind survives close")
  local read_ok, read_err = pcall(function() return fs:read_file("DBFilesClient/Map.dbc") end)
  check(not read_ok, "read after close errors")
  check(tostring(read_err):find("StorageNotOpen") ~= nil, "closed error carries the code")
  fs:close() -- second close is a no-op

  print("real-client round-trip OK, " .. #data .. " bytes")
end

print("wowlib lua module OK")