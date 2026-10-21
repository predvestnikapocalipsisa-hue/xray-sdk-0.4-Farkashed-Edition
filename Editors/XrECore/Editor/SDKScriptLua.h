#pragma once
#include <string>
namespace SDKScriptLua
{
    std::string Execute(const std::string& owner, const std::string& source, bool expression);
    void RemoveOwner(const std::string& owner);
    void Shutdown();
}
