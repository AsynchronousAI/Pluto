/*
** $Id: linit.c $
** Initialization of libraries for lua.c and other clients
** See Copyright Notice in lua.h
*/


#define linit_c
#define LUA_LIB

/*
** If you embed Lua in your program and need to open the standard
** libraries, call luaL_openlibs in your program. If you need a
** different set of libraries, copy this file to your project and edit
** it to suit your needs.
**
** You can also *preload* libraries, so that a later 'require' can
** open the library, which is already linked to the application.
** For that, do the following code:
**
**  luaL_getsubtable(L, LUA_REGISTRYINDEX, LUA_PRELOAD_TABLE);
**  lua_pushcfunction(L, luaopen_modname);
**  lua_setfield(L, -2, modname);
**  lua_pop(L, 1);  // remove PRELOAD table
*/

#include "lprefix.h"


#include <stddef.h>
#include <cstring>

#include "lua.h"

#include "lualib.h"
#include "lauxlib.h"


/*
** these libs are loaded by lua.c and are readily available to any Lua
** program
*/
static const luaL_Reg loadedlibs[] = {
  {LUA_GNAME, luaopen_base},
  {LUA_LOADLIBNAME, luaopen_package},
  {LUA_COLIBNAME, luaopen_coroutine},
  {LUA_TABLIBNAME, luaopen_table},
#ifndef PLUTO_NO_FILESYSTEM
  {LUA_IOLIBNAME, luaopen_io},
#endif
  {LUA_OSLIBNAME, luaopen_os},
  {LUA_STRLIBNAME, luaopen_string},
  {LUA_MATHLIBNAME, luaopen_math},
  {LUA_UTF8LIBNAME, luaopen_utf8},
  {LUA_DBLIBNAME, luaopen_debug},
  {NULL, NULL}
};


LUALIB_API void luaL_openlibs (lua_State *L) {
  const luaL_Reg *lib;

  for (lib = loadedlibs; lib->func; lib++) {
    luaL_requiref(L, lib->name, lib->func, 1);
    lua_pop(L, 1);  /* remove lib */
  }

  for (const Pluto::PreloadedLibrary* lib : Pluto::all_preloaded) {
    luaL_getsubtable(L, LUA_REGISTRYINDEX, LUA_PRELOAD_TABLE);
    lua_pushcfunction(L, lib->init);
    lua_setfield(L, -2, lib->name);
    lua_pop(L, 1);
  }

  const auto startup_code = R"EOC(
pluto_class Exception
    __name = "Exception"

    function __construct(public what)
        local caller
        local i = 2
        while true do
            caller = debug.getinfo(i)
            if caller == nil then
                error("Exception instances must be created with 'pluto_new'", 0)
            end
            ++i
            if caller.name == "Pluto_operator_new" then
                caller = debug.getinfo(i)
                break
            end
        end
        self.where = $"{caller.short_src}:{caller.currentline}"
        error(self, 0)
    end

    function __tostring()
        return $"{self.where}: {tostring(self.what)}"
    end
end

function instanceof(a, b)
  return a instanceof b
end

package.preload["assert"] = function()
  local module = {}

  local function deepCompare(t1, t2)
    if t1 == t2 then
      return true
    end

    if #t1 ~= #t2 then
      return false
    end

    if next(t1) == nil and next(t2) ~= nil then
      return false
    end

    for k, v1 in t1 do
      local v2 = t2[k]
        
      if type(v1) == "table" and type(v2) == "table" and v1 ~= v2 then
        if not deepCompare(v1, v2) then
          return false
        end
      elseif v1 ~= v2 then
        return false
      end
    end

    return true
  end

  local class AssertionError
    function __construct(assertion_name: string, public intended_value, public received_value)
      self.assertion_name = "assert." .. assertion_name
      self.should_dump = true
      self.write_intended = true
      self.write_recieved = true

      self.intended_name = "Intended Value"
      self.received_name = "Received Value"

      return self
    end

    function raise()
      local message = self:getFileInformation() .. " -> Assertion Error: " .. self:getExtraInformation()

      if self.write_intended then
        message ..= "\n\t"
        message ..= self.intended_name .. ": " .. self:dumpValue(self.intended_value, self.hardcoded ? false : self.should_dump)
      end

      if self.write_recieved then
        message ..= "\n\t"
        message ..= self.received_name .. ": " .. self:dumpValue(self.received_value, self.should_dump)
      end

      if self.operator_comparison then
        local n1 = self:dumpValue(self.intended_value)
        local n2 = self:dumpValue(self.received_value)
        local op = self.operator_comparison_operator

        message ..= "\n\t"
        message ..= $"Expression: ({n1} {op} {n2}) == false"
      end

      if self.simple then
        message ..= "\n\t"
        message ..= self.simple_message
      end

      message ..= "\n" -- Before traceback.

      error(message, 0)
    end

    function dumpValue(value, should_dump: bool = self.should_dump): string
      if type(value) == "function" or should_dump == false then
        return tostring(value)
      end

      local dump = dumpvar(value) -- dumpvar will still serialize subfunctions, need to fix that.

      if type(value) == "table" then
        if dump:len() < 80 then
          dump = dump:replace("\n", " "):replace("\t", "")
        else
          dump = "\n" .. dump:truncate(300, true)
        end
      end

      return dump
    end

    function getFileInformation(): string
      local caller

      for i = 2, 255 do
        caller = debug.getinfo(i)
        if caller and not tostring(caller.short_src):contains("[") then
          break
        end
      end

      local short_src = caller?.short_src ?? "unk"
      local currentline = caller?.currentline ?? "0"

      return $"{short_src}:{currentline}"
    end

    function getExtraInformation(): string
      return $"({self.assertion_name})"
    end

    function setDontDump() -- Applies to both the intended and received values.
      self.should_dump = false

      return self
    end

    function setHardcoded() -- Prevents 'Intended Value' from being dumped.
      self.hardcoded = true

      return self
    end

    function setOperatorComparison(operator: string) -- Expression: N operator N (replaces intended/received)
      self.operator_comparison = true
      self.operator_comparison_operator = operator
      self.write_intended = false
      self.write_recieved = false

      return self
    end

    function setSimple(message: string)
      self.write_recieved = false
      self.write_intended = false
      self.simple = true
      self.simple_message = message

      return self
    end

    function setNameOverride(intended: string, received: string)
      if intended then
        self.intended_name = intended
      end

      if received then
        self.received_name = received
      end

      return self
    end
  end

  function module.isnil(value)
    if value ~= nil then
      return new AssertionError("isnil", "nil", value):setHardcoded():raise()
    end
  end

  function module.istrue(value)
    if value ~= true then
      return new AssertionError("istrue", "true", value):setHardcoded():raise()
    end
  end

  function module.isfalse(value)
    if value ~= false then
      return new AssertionError("isfalse", "false", value):setHardcoded():raise()
    end
  end

  function module.falsy(value)
    if value then -- It's truthy
      return new AssertionError("falsy", "nil or false", value):setHardcoded():raise()
    end
  end

  function module.truthy(value)
    if not value then
      return new AssertionError("falsy", "not nil or false", value):setHardcoded():raise()
    end
  end

   function module.notnil(value)
    if value == nil then
      return new AssertionError("notnil", "not nil", value):setHardcoded():raise()
    end
  end

  function module.equal(value1, value2)
    if type(value1) == "table" and type(value2) == "table" then
      if not deepCompare(value1, value2) then
        return new AssertionError("equal", value1, value2):raise()
      end
    else
      if value1 ~= value2 then
        return new AssertionError("equal", value1, value2):raise()
      end
    end
  end

  function module.nequal(value1, value2)
    if type(value1) == "table" and type(value2) == "table" then
      if deepCompare(value1, value2) then
        return new AssertionError("nequal", value1, value2):raise()
      end
    else
      if value1 == value2 then
        return new AssertionError("nequal", value1, value2):raise()
      end
    end
  end

  function module.lessthan(value1, value2)
    if not (value1 < value2) then
      return new AssertionError("lessthan", value1, value2):setOperatorComparison("<"):raise()
    end
  end

  function module.lessthaneq(value1, value2)
    if not (value1 <= value2) then
      return new AssertionError("lessthaneq", value1, value2):setOperatorComparison("<="):raise()
    end
  end

  function module.greaterthan(value1, value2)
    if not (value1 > value2) then
      return new AssertionError("greaterthan", value1, value2):setOperatorComparison(">"):raise()
    end
  end

  function module.greaterthaneq(value1, value2)
    if not (value1 >= value2) then
      return new AssertionError("greaterthaneq", value1, value2):setOperatorComparison(">="):raise()
    end
  end

  function module.noerror(callback, ...)
    local status, err = pcall(callback, ...)
    if status == false then
      return new AssertionError("noerror", nil, nil):setSimple("An error was raised: " .. tostring(err)):raise()
    end
  end

  function module.haserror(callback, ...)
    local status, err = pcall(callback, ...)
    if status == true then
      return new AssertionError("haserror", nil, nil):setSimple("Expected an error, but there was none."):raise()
    end
  end

  function module.containserror(substring, callback, ...)
    local status, err = pcall(callback, ...)
    if status == true then
      return new AssertionError("containserror", nil, nil):setSimple("Expected an error, but there was none."):raise()
    elseif not tostring(err):contains(substring) then
      return new AssertionError("containserror", substring, tostring(err)):setDontDump():setNameOverride("Absent String", "Error Message"):raise()
    end
  end

  function module.type(desired_type, ...)
    local t = {...}
    for i = 1, #t do
      local v = t[i]
      if type(v) ~= desired_type then
        return new AssertionError($"type, argument #{i}", desired_type, v):setHardcoded():setNameOverride("Intended Type"):raise()
      end
    end
  end

  setmetatable(module, {
    __call = function (self, cond, err_msg = "assertion failed!")
      err_msg = " " .. err_msg
      if not cond then
        return new AssertionError("assert", nil, nil):setSimple(err_msg):raise()
      end
    end
  })

  module.AssertionError = AssertionError

  return module
end
)EOC";
  luaL_loadbuffer(L, startup_code, strlen(startup_code), "Pluto Standard Library");
  lua_call(L, 0, 0);
}

