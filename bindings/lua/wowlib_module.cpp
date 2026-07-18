// The Lua module, via the LuaBridge3 rod: `require("wowlib")` loads it, names are
// snake_case (matching the C++ spelling), and the same annotated headers drive
// the bindings as in Python.
//
// LuaBridge3 lacks native conversions for a few wowlib vocabulary types, so this
// TU teaches them with Stack specializations (the extension point LuaBridge3's
// own Vector.h/Map.h use):
// - Result<T>: success pushes T; failure throws, which LuaBridge3 converts into a
//   lua error "<ErrorCode>: <message>" at the call boundary.
// - FileBuffer / std::span<const std::byte>: Lua strings (Lua strings are byte
//   arrays; spans view the interned Lua string, valid for the call's duration).
// - std::filesystem::path: Lua strings.
#include <cstring>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

#include <welder/vocabulary.hpp>

#include <welder/rods/lua/luabridge/module.hpp>

#include <wowlib/wowlib.hpp>

namespace luabridge
{
  template <typename T>
  struct Stack<wowlib::Result<T>>
  {
    // by value: the success payload moves through (several wowlib types are
    // move-only), and LuaBridge hands us the callable's prvalue anyway
    [[nodiscard]] static Result push(lua_State* L, wowlib::Result<T> result)
    {
      if (!result)
        throw std::runtime_error(std::string{wowlib::to_string(result.error().code)} +
                                 ": " + result.error().message);
      return Stack<T>::push(L, std::move(*result));
    }

    // Result is a return-only vocabulary type; it never crosses Lua -> C++.
    [[nodiscard]] static TypeResult<wowlib::Result<T>> get(lua_State*, int)
    {
      return makeErrorCode(ErrorCode::InvalidTypeCast);
    }

    [[nodiscard]] static bool isInstance(lua_State*, int) { return false; }
  };

  template <>
  struct Stack<wowlib::Result<void>>
  {
    [[nodiscard]] static Result push(lua_State* L, const wowlib::Result<void>& result)
    {
      if (!result)
        throw std::runtime_error(std::string{wowlib::to_string(result.error().code)} +
                                 ": " + result.error().message);
      lua_pushboolean(L, 1);
      return {};
    }

    [[nodiscard]] static TypeResult<wowlib::Result<void>> get(lua_State*, int)
    {
      return makeErrorCode(ErrorCode::InvalidTypeCast);
    }

    [[nodiscard]] static bool isInstance(lua_State*, int) { return false; }
  };

  template <>
  struct Stack<wowlib::FileBuffer>
  {
    [[nodiscard]] static Result push(lua_State* L, const wowlib::FileBuffer& buffer)
    {
      lua_pushlstring(L, reinterpret_cast<const char*>(buffer.data()), buffer.size());
      return {};
    }

    [[nodiscard]] static TypeResult<wowlib::FileBuffer> get(lua_State* L, int index)
    {
      std::size_t size = 0;
      const char* data = lua_tolstring(L, index, &size);
      if (!data)
        return makeErrorCode(ErrorCode::InvalidTypeCast);
      wowlib::FileBuffer buffer(size);
      std::memcpy(buffer.data(), data, size);
      return buffer;
    }

    [[nodiscard]] static bool isInstance(lua_State* L, int index)
    {
      return lua_type(L, index) == LUA_TSTRING;
    }
  };

  template <>
  struct Stack<std::span<const std::byte>>
  {
    [[nodiscard]] static Result push(lua_State* L, std::span<const std::byte> data)
    {
      lua_pushlstring(L, reinterpret_cast<const char*>(data.data()), data.size());
      return {};
    }

    [[nodiscard]] static TypeResult<std::span<const std::byte>> get(lua_State* L,
                                                                    int index)
    {
      std::size_t size = 0;
      const char* data = lua_tolstring(L, index, &size);
      if (!data)
        return makeErrorCode(ErrorCode::InvalidTypeCast);
      // views the interned Lua string; Lua guarantees it for the call's duration
      return std::span<const std::byte>{reinterpret_cast<const std::byte*>(data), size};
    }

    [[nodiscard]] static bool isInstance(lua_State* L, int index)
    {
      return lua_type(L, index) == LUA_TSTRING;
    }
  };

  // (std::filesystem::path and std::optional cross natively — LuaBridge3 ships
  // Stack specializations for both in detail/Stack.h)
}

WELDER_MODULE(wowlib, luabridge,
              welder::welder<welder::rods::luabridge::rod, welder::naming::snake_case>)
{
}