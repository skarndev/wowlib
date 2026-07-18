-- Smoke check for the wowlib Lua module (run by ctest with LUA_CPATH set).
-- Names are snake_case throughout — welder's Lua naming convention reshapes
-- types, members and enumerators alike (ClientVersion -> client_version,
-- StorageKind.Mpq -> storage_kind.mpq, Locale.enUS -> locale.en_us).

local wowlib = require("wowlib")

local function check(condition, message)
  if not condition then
    print("FAIL: " .. message)
    os.exit(1)
  end
end

-- enums and version factories
check(wowlib.client_version.wotlk().build == 12340, "wotlk build")
check(wowlib.client_version.tww().build == 65299, "tww build")
check(wowlib.client_version.wotlk():storage_kind() == wowlib.storage_kind.mpq,
      "kind mpq")
check(wowlib.client_version.shadowlands():storage_kind() == wowlib.storage_kind.casc,
      "kind casc")
check(wowlib.locale.en_us ~= nil, "locale enum")

-- FileKey constructor overloads + canonicalization
local key = wowlib.file_key("World/Maps/Azeroth/Azeroth.wdt")
check(key.path == "world\\maps\\azeroth\\azeroth.wdt", "path canonicalized")
key = wowlib.file_key(wowlib.file_data_id(775971))
check(key.fdid.value == 775971, "id-only key")

-- error translation: a failed Result raises a lua error carrying the code
local settings = wowlib.file_system_settings()
settings.client_path = "/no/such/client"
settings.version = wowlib.client_version.wotlk()
local ok, err = pcall(function() return wowlib.file_system.open(settings) end)
check(not ok, "open should have raised")
check(tostring(err):find("StorageOpenFailed") ~= nil,
      "error message carries the code: " .. tostring(err))

-- real-client round-trip (opt-in via env)
local clients = os.getenv("WOWLIB_TEST_CLIENTS_DIR")
if clients then
  settings = wowlib.file_system_settings()
  settings.client_path = clients .. "/World of Warcraft 3.3.5a"
  settings.version = wowlib.client_version.wotlk()
  local fs = wowlib.file_system.open(settings)
  check(fs:kind() == wowlib.storage_kind.mpq, "facade kind")
  local data = fs:read_file("DBFilesClient/Map.dbc")
  check(type(data) == "string" and data:sub(1, 4) == "WDBC", "Map.dbc magic via Lua")
  print("real-client round-trip OK, " .. #data .. " bytes")
end

print("wowlib lua module OK")