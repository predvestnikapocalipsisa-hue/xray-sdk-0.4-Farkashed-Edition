#include "stdafx.h"
#include "SDKScriptHost.h"
#include "SDKScriptLua.h"
#include <lua.hpp>
#include <luajit.h>
#include <map>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <cmath>

namespace SDKScriptLua
{
    namespace
    {
        HMODULE module = nullptr;
        std::map<std::string,lua_State*> states;
        ULONGLONG deadline = 0;
#define LUA_FUNCTIONS(X) X(luaL_newstate) X(luaL_openlibs) X(luaL_loadbuffer) X(lua_close) X(lua_pcall) X(lua_gettop) X(lua_settop) X(lua_type) X(lua_tolstring) X(lua_tonumber) X(lua_toboolean) X(lua_objlen) X(lua_rawgeti) X(lua_pushnil) X(lua_next) X(lua_pushcclosure) X(lua_createtable) X(lua_setfield) X(lua_pushstring) X(lua_sethook) X(lua_error) X(luaJIT_setmode)
#define DECLARE(name) decltype(&name) p_##name = nullptr;
        LUA_FUNCTIONS(DECLARE)
#undef DECLARE
        void Load()
        {
            if(module) return;
            wchar_t path[MAX_PATH] = {};
            GetModuleFileNameW(GetModuleHandleW(L"SDKRuntime.dll"),path,MAX_PATH);
            wchar_t* slash=wcsrchr(path,L'\\'); if(slash) slash[1]=0;
            std::wstring library=std::wstring(path)+L"LuaJIT.dll";
            module=LoadLibraryExW(library.c_str(),nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
            if(!module) throw std::runtime_error("LuaJIT runtime is unavailable");
#define LOAD(name) p_##name=reinterpret_cast<decltype(p_##name)>(GetProcAddress(module,#name)); if(!p_##name) { module=nullptr; throw std::runtime_error("Missing LuaJIT export: " #name); }
            LUA_FUNCTIONS(LOAD)
#undef LOAD
        }
        void Hook(lua_State* state,lua_Debug*)
        {
            if(GetTickCount64()>deadline) { p_lua_pushstring(state,"Lua execution exceeded 2 seconds"); p_lua_error(state); }
        }
        int Log(lua_State* state)
        {
            size_t length=0; const char* value=p_lua_tolstring(state,1,&length);
            SDKScriptCall("log",value?value:"",0,0,0); return 0;
        }
        int Command(lua_State* state)
        {
            const char* name=p_lua_tolstring(state,1,nullptr);
            if(!name) { p_lua_pushnil(state); return 1; }
            std::string text=name;
            for(int index=2;index<=3;++index)
            {
                text+='\x1f';
                if(p_lua_type(state,index)==LUA_TSTRING) { text+='s'; text+=p_lua_tolstring(state,index,nullptr); }
                else { text+='i'; text+=std::to_string(static_cast<u32>(p_lua_tonumber(state,index))); }
            }
            const char* result=SDKScriptCall("execute",text.c_str(),0,0,0);
            p_lua_pushstring(state,result); return 1;
        }
        std::string Value(lua_State* state,int index,unsigned depth)
        {
            if(depth>16) throw std::runtime_error("Lua return table is cyclic or too deep");
            const int kind=p_lua_type(state,index);
            if(kind==LUA_TNIL) return "null";
            if(kind==LUA_TBOOLEAN) return p_lua_toboolean(state,index)?"true":"false";
            if(kind==LUA_TNUMBER)
            {
                const double number=p_lua_tonumber(state,index);
                if(!std::isfinite(number)) throw std::runtime_error("Lua returned a non-finite number");
                std::ostringstream out; out << std::setprecision(17) << number; return out.str();
            }
            if(kind==LUA_TSTRING) { size_t size=0; const char* text=p_lua_tolstring(state,index,&size); return SDKScript::Quote(std::string(text,size)); }
            if(kind!=LUA_TTABLE) throw std::runtime_error("Lua returns must be nil, boolean, number, UTF-8 string, array or string-keyed table");
            if(index<0) index=p_lua_gettop(state)+index+1;
            const size_t length=p_lua_objlen(state,index);
            size_t count=0; bool array=length>0;
            p_lua_pushnil(state);
            while(p_lua_next(state,index))
            {
                ++count;
                if(p_lua_type(state,-2)!=LUA_TNUMBER) array=false;
                else { double key=p_lua_tonumber(state,-2); if(key<1 || key>double(length) || key!=floor(key)) array=false; }
                p_lua_settop(state,-2);
            }
            array=array && count==length;
            std::string result=array?"[":"{";
            if(array)
            {
                for(size_t i=1;i<=length;++i) { p_lua_rawgeti(state,index,int(i)); if(i>1) result+=","; result+=Value(state,-1,depth+1); p_lua_settop(state,-2); }
            }
            else
            {
                p_lua_pushnil(state);
                while(p_lua_next(state,index))
                {
                    if(p_lua_type(state,-2)!=LUA_TSTRING) throw std::runtime_error("Mixed Lua table keys cannot be converted to a Python dictionary");
                    if(result.size()>1) result+=",";
                    result+=SDKScript::Quote(p_lua_tolstring(state,-2,nullptr))+":"+Value(state,-1,depth+1);
                    p_lua_settop(state,-2);
                }
            }
            return result+(array?"]":"}");
        }
    }
    std::string Execute(const std::string& owner,const std::string& source,bool expression)
    {
        Load(); lua_State*& state=states[owner];
        if(!state)
        {
            state=p_luaL_newstate(); if(!state) throw std::bad_alloc();
            p_luaL_openlibs(state);
            p_luaJIT_setmode(state,0,LUAJIT_MODE_ENGINE|LUAJIT_MODE_OFF);
            p_lua_createtable(state,0,2);
            p_lua_pushcclosure(state,Log,0); p_lua_setfield(state,-2,"log");
            p_lua_pushcclosure(state,Command,0); p_lua_setfield(state,-2,"command");
            p_lua_setfield(state,LUA_GLOBALSINDEX,"sdk");
        }
        struct Stack { lua_State* state; ~Stack() { p_lua_sethook(state,nullptr,0,0); p_lua_settop(state,0); } } stack{state};
        const std::string code=expression ? "return ("+source+")" : source;
        if(p_luaL_loadbuffer(state,code.data(),code.size(),owner.c_str())) throw std::runtime_error(p_lua_tolstring(state,-1,nullptr));
        deadline=GetTickCount64()+2000; p_lua_sethook(state,Hook,LUA_MASKCOUNT,10000);
        if(p_lua_pcall(state,0,1,0)) throw std::runtime_error(p_lua_tolstring(state,-1,nullptr));
        return Value(state,-1,0);
    }
    void RemoveOwner(const std::string& owner)
    {
        auto found=states.find(owner); if(found!=states.end()) { if(found->second) p_lua_close(found->second); states.erase(found); }
    }
    void Shutdown() { for(auto item:states) if(item.second) p_lua_close(item.second); states.clear(); }
}
