#include "stdafx.h"
#include "SDKScriptHost.h"
#include "SDKScriptLua.h"
#include "ui_main.h"
#include "UI_MainCommand.h"
#include "device.h"
#include "../../XrEUI/imgui.h"
#include <commdlg.h>
#include <shellapi.h>
#include <deque>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <mutex>
#include <cmath>
#pragma comment(lib,"comdlg32.lib")
#pragma comment(lib,"shell32.lib")

namespace SDKScript
{
    namespace
    {
        struct Registration { std::string id, title, owner, shortcut; bool open = true; };
        std::vector<Registration> operators, panels;
        std::deque<std::pair<std::string,std::string>> events;
        std::deque<std::string> output;
        std::mutex pendingLogMutex;
        std::deque<std::string> pendingLogs;
        Backend backend = nullptr;
        HMODULE python = nullptr;
        void(__cdecl* initialize)(int) = nullptr;
        int(__cdecl* run)(const char*,void*) = nullptr;
        int(__cdecl* finalize)() = nullptr;
        void(__cdecl* setPath)(const wchar_t*) = nullptr;
        DWORD ownerThread = 0;
        bool attempted = false, ready = false, closing = false, drawing = false;
        bool showManager = false, showConsole = false;
        std::string failure, editor, root;
        std::wstring pythonPath;
        char console[16384] = {};

        std::vector<std::string> Split(const std::string& value)
        {
            std::vector<std::string> result;
            size_t from = 0, at;
            do { at = value.find('\x1f',from); result.push_back(value.substr(from,at-from)); from = at+1; } while (at != std::string::npos);
            return result;
        }
        void Log(const std::string& line)
        {
            if (output.size() >= 1000) output.pop_front();
            output.push_back(line);
            const std::string ansi = FromUTF8(line);
            Msg("[Python] %s",ansi.c_str());
        }
        void Run(const std::string& code)
        {
            if (ready && run(code.c_str(),nullptr) != 0) Log("Python host call failed (see traceback above)");
        }
        void Start()
        {
            if (attempted) return;
            attempted = true; ownerThread = GetCurrentThreadId(); editor = UI->EditorName();
            string_path path;
            FS.update_path(path,"$sdk_root$",""); root = ToUTF8(path);
            std::wstring binary(32768,L'\0');
            DWORD length = GetModuleFileNameW(GetModuleHandleW(L"SDKRuntime.dll"),&binary[0],DWORD(binary.size()));
            binary.resize(length); binary.resize(binary.find_last_of(L"\\/"));
            const std::wstring directory = binary+L"\\python";
            python = LoadLibraryExW((directory+L"\\python313.dll").c_str(),nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
            if (!python) { failure = "Python runtime missing. Install bins/python with the SDK scripting installer. Windows error: "+std::to_string(GetLastError()); Log(failure); FlushLog(); return; }
            initialize = reinterpret_cast<decltype(initialize)>(GetProcAddress(python,"Py_InitializeEx"));
            run = reinterpret_cast<decltype(run)>(GetProcAddress(python,"PyRun_SimpleStringFlags"));
            finalize = reinterpret_cast<decltype(finalize)>(GetProcAddress(python,"Py_FinalizeEx"));
            setPath = reinterpret_cast<decltype(setPath)>(GetProcAddress(python,"Py_SetPath"));
            if (!initialize || !run || !finalize || !setPath) { failure = "Unsupported Python runtime ABI"; Log(failure); FlushLog(); return; }
            pythonPath = directory+L"\\python313.zip;"+directory+L";"+binary+L"\\scripting";
            setPath(pythonPath.c_str()); initialize(0); ready = true;
            const std::string code = "import xray\nxray._start("+Quote(root)+","+Quote(editor)+")";
            if (run(code.c_str(),nullptr)) { failure = "Cannot load xray Python package"; Log(failure); FlushLog(); ready = false; }
        }
        bool Shortcut(const std::string& value)
        {
            if (value.empty() || ImGui::GetIO().WantTextInput) return false;
            const auto& io = ImGui::GetIO();
            if (io.KeyCtrl != (value.find("CTRL+") != std::string::npos) ||
                io.KeyShift != (value.find("SHIFT+") != std::string::npos) ||
                io.KeyAlt != (value.find("ALT+") != std::string::npos)) return false;
            const char key = value.back();
            return key >= 'A' && key <= 'Z' && ImGui::IsKeyPressed(ImGuiKey(ImGuiKey_A+key-'A'),false);
        }
        std::string OpenScript(bool install)
        {
            wchar_t file[32768] = {};
            OPENFILENAMEW dialog = {sizeof(dialog)};
            dialog.hwndOwner = EDevice.m_hWnd; dialog.lpstrFile = file; dialog.nMaxFile = 32768;
            dialog.lpstrFilter = install ? L"Add-ons (*.xpy;*.zip)\0*.xpy;*.zip\0\0" : L"Python scripts (*.xpy)\0*.xpy\0\0";
            dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
            if (!GetOpenFileNameW(&dialog)) return {};
            const int count = WideCharToMultiByte(CP_UTF8,0,file,-1,nullptr,0,nullptr,nullptr);
            std::string result(size_t(count),0); WideCharToMultiByte(CP_UTF8,0,file,-1,&result[0],count,nullptr,nullptr);
            result.pop_back(); return result;
        }
    }

    std::string Quote(const std::string& value)
    {
        std::string result = "\"";
        for (unsigned char c : value)
        {
            if (c == '"' || c == '\\') { result += '\\'; result += char(c); }
            else if (c < 32) { char escaped[8]; sprintf_s(escaped,"\\u%04x",c); result += escaped; }
            else result += char(c);
        }
        return result+'"';
    }
    std::string Convert(const std::string& text, UINT from, UINT to)
    {
        if (text.empty()) return {};
        const int n = MultiByteToWideChar(from,0,text.data(),int(text.size()),nullptr,0);
        std::wstring wide(size_t(n),0); MultiByteToWideChar(from,0,text.data(),int(text.size()),&wide[0],n);
        const int m = WideCharToMultiByte(to,0,wide.data(),n,nullptr,0,nullptr,nullptr);
        std::string result(size_t(m),0); WideCharToMultiByte(to,0,wide.data(),n,&result[0],m,nullptr,nullptr); return result;
    }
    std::string FromUTF8(const std::string& text) { return Convert(text,CP_UTF8,CP_ACP); }
    std::string ToUTF8(const char* text) { return Convert(text ? text : "",CP_ACP,CP_UTF8); }
    void SetBackend(Backend value) { backend = value; }

    std::string Call(const std::string& op,const std::string& text,double x,double y,double z)
    {
        if (GetCurrentThreadId() != ownerThread)
        {
            if(op=="log") { std::lock_guard<std::mutex> lock(pendingLogMutex); if(pendingLogs.size()<1000) pendingLogs.push_back(text); return "null"; }
            throw std::runtime_error("The editor API must be called on the UI thread; use xray.timers");
        }
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) throw std::runtime_error("Non-finite bridge argument");
        if (closing && op != "log" && op != "owner_remove") throw std::runtime_error("Editor is shutting down");
        if (op == "log") { Log(text); return "null"; }
        if (op == "context") return "{\"editor\":"+Quote(editor)+",\"sdk_root\":"+Quote(root)+",\"api_version\":1}";
        if (op == "request_exit") { PostMessageW(EDevice.m_hWnd,WM_CLOSE,0,0); return "null"; }
        if (op == "owner_remove")
        {
            SDKScriptLua::RemoveOwner(text);
            auto remove = [&](std::vector<Registration>& list) { list.erase(std::remove_if(list.begin(),list.end(),[&](const Registration& r){return r.owner == text;}),list.end()); };
            remove(operators); remove(panels); return "null";
        }
        if (op == "lua.eval" || op == "lua.execute")
        {
            const auto parts=Split(text); if(parts.size()!=2) throw std::runtime_error("Invalid Lua arguments");
            return SDKScriptLua::Execute(parts[0],parts[1],op=="lua.eval");
        }
        if (op == "lua.reset") { SDKScriptLua::RemoveOwner(text); return "null"; }
        if (op == "register_operator" || op == "register_panel")
        {
            auto parts = Split(text);
            if (parts.size() != 4 || parts[0].empty() || parts[1].empty()) throw std::runtime_error("Invalid registration");
            auto& list = op == "register_operator" ? operators : panels;
            if (std::find_if(list.begin(),list.end(),[&](const Registration& r){return r.id == parts[0];}) != list.end()) throw std::runtime_error("Duplicate registration id");
            list.push_back({parts[0],parts[1],parts[2],parts[3],true}); return "null";
        }
        if (op == "commands")
        {
            std::string result = "[";
            for (auto* command : GetEditorCommands()) if (command) { if (result.size()>1) result+=","; result+=Quote(command->Name()); }
            return result+"]";
        }
        if (op == "execute")
        {
            auto parts = Split(text);
            if (parts.size()!=3) throw std::runtime_error("Invalid command arguments");
            auto* command = FindCommandByName(parts[0].c_str());
            if (!command) throw std::runtime_error("Unknown editor command");
            if (command->idx == COMMAND_INITIALIZE || command->idx == COMMAND_DESTROY || command->idx == COMMAND_QUIT || command->idx == COMMAND_EXIT)
                throw std::runtime_error("Editor lifecycle commands cannot run inside Python");
            auto argument = [](const std::string& value)
            {
                if(value.empty()) throw std::runtime_error("Empty command argument");
                if(value[0]=='s') return CCommandVar(xr_string(FromUTF8(value.substr(1)).c_str()));
                if(value[0]!='i' || value.size()<2 || value.find_first_not_of("0123456789",1)!=std::string::npos) throw std::runtime_error("Invalid command argument");
                const auto number=std::stoull(value.substr(1));
                if(number>0xffffffffULL) throw std::runtime_error("Command argument exceeds uint32");
                return CCommandVar(u32(number));
            };
            CCommandVar result = ExecCommand(command->idx,argument(parts[1]),argument(parts[2]));
            return result.IsString() ? Quote(ToUTF8(xr_string(result).c_str())) : std::to_string(u32(result));
        }
        if (op.compare(0,3,"ui.")==0)
        {
            if (!drawing) throw std::runtime_error("UI widgets may only be used in a panel draw callback");
            if (op == "ui.text") { ImGui::TextUnformatted(text.c_str()); return "null"; }
            if (op == "ui.separator") { ImGui::Separator(); return "null"; }
            if (op == "ui.same_line") { ImGui::SameLine(); return "null"; }
            if (op == "ui.button") return ImGui::Button(text.c_str()) ? "true" : "false";
            if (op == "ui.header") return ImGui::CollapsingHeader(text.c_str()) ? "true" : "false";
            if (op == "ui.checkbox") { bool value=x!=0; ImGui::Checkbox(text.c_str(),&value); return value ? "true" : "false"; }
            if (op == "ui.float") { float value=float(x); ImGui::InputFloat(text.c_str(),&value,float(y),float(z)); return std::to_string(value); }
            if (op == "ui.slider") { float value=float(x); ImGui::SliderFloat(text.c_str(),&value,float(y),float(z)); return std::to_string(value); }
            if (op == "ui.input")
            {
                auto parts = Split(text); if (parts.size()!=2 || parts[1].size()>4094) throw std::runtime_error("Invalid input text");
                char buffer[4096]; strcpy_s(buffer,parts[1].c_str()); ImGui::InputText(parts[0].c_str(),buffer,sizeof(buffer)); return Quote(buffer);
            }
            throw std::runtime_error("Unknown UI widget");
        }
        if (backend) return backend(op,text,x,y,z);
        throw std::runtime_error("This operation is not supported in the current editor");
    }

    void CommandEvent(const char* name,bool before)
    {
        if (!ready || closing) return;
        const std::string command = name;
        if (command.find("UPDATE") != std::string::npos || command.find("RENDER") != std::string::npos || command.find("REFRESH") != std::string::npos) return;
        if (before && backend && (command == "COMMAND_LOAD" || command == "COMMAND_CLEAR" || command == "COMMAND_UNDO" || command == "COMMAND_REDO"))
            backend("invalidate",command,0,0,0);
        if (!before && events.size()<256) events.push_back({"command_executed",command});
    }
    void Tick()
    {
        if(closing) return;
        static ULONGLONG nextTick=0;
        const auto now=GetTickCount64(); if(now<nextTick) return; nextTick=now+16;
        Start();
        { std::deque<std::string> lines; { std::lock_guard<std::mutex> lock(pendingLogMutex); lines.swap(pendingLogs); } for(const auto& line:lines) Log(line); }
        const auto pending=events; events.clear();
        for(const auto& event:pending) Run("xray._event("+Quote(event.first)+","+Quote(event.second)+")");
        Run("xray._tick()");
    }
    void Draw()
    {
        Tick();
        std::string invoke;
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("Scripts"))
            {
                ImGui::MenuItem("Add-on Manager",nullptr,&showManager);
                ImGui::MenuItem("Python Console",nullptr,&showConsole);
                if (ImGui::MenuItem("Run .xpy file...",nullptr,false,ready)) { const auto file=OpenScript(false); if (!file.empty()) Run("xray._run_file("+Quote(file)+")"); }
                if (ImGui::BeginMenu("Panels")) { for (auto& panel : panels) ImGui::MenuItem(panel.title.c_str(),nullptr,&panel.open); ImGui::EndMenu(); }
                ImGui::Separator();
                for (const auto& command : operators) if (ImGui::MenuItem((command.title+"##"+command.id).c_str(),command.shortcut.c_str())) invoke=command.id;
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }
        for (const auto& command : operators) if (Shortcut(command.shortcut)) invoke=command.id;
        if (!invoke.empty()) Run("xray._invoke("+Quote(invoke)+")");
        if (showManager)
        {
            ImGui::SetNextWindowSize(ImVec2(650,500),ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Add-on Manager",&showManager))
            {
                if (!failure.empty()) ImGui::TextWrapped("%s",failure.c_str());
                if (ready)
                {
                    if (ImGui::Button("Refresh")) Run("xray.addons.refresh()");
                    ImGui::SameLine(); if (ImGui::Button("Install .xpy / .zip...")) { const auto file=OpenScript(true); if (!file.empty()) Run("xray.addons.install("+Quote(file)+")"); }
                    ImGui::SameLine(); if (ImGui::Button("New add-on")) Run("xray.addons.create_template()");
                    ImGui::SameLine(); if (ImGui::Button("Open folder")) Run("xray.addons.open_folder()");
                    ImGui::Separator(); drawing=true; Run("xray._draw_manager()"); drawing=false;
                }
            }
            ImGui::End();
        }
        const auto visiblePanels=panels;
        for (const auto& panel : visiblePanels) if (panel.open)
        {
            bool open=true; ImGui::SetNextWindowSize(ImVec2(350,250),ImGuiCond_FirstUseEver);
            if (ImGui::Begin((panel.title+"###addon_"+panel.id).c_str(),&open)) { drawing=true; Run("xray._draw_panel("+Quote(panel.id)+")"); drawing=false; }
            ImGui::End();
            if (!open) for (auto& current : panels) if (current.id==panel.id) current.open=false;
        }
        if (showConsole)
        {
            ImGui::SetNextWindowSize(ImVec2(800,500),ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Python Console",&showConsole))
            {
                ImGui::BeginChild("Console output",ImVec2(0,-150));
                for (const auto& line : output) ImGui::TextUnformatted(line.c_str());
                ImGui::EndChild();
                ImGui::InputTextMultiline("##Python",console,sizeof(console),ImVec2(-1,100));
                if (ImGui::Button("Execute") && ready) Run("xray._console_exec("+Quote(console)+")");
                ImGui::SameLine(); if (ImGui::Button("Clear output")) output.clear();
                if (!failure.empty()) ImGui::TextWrapped("%s",failure.c_str());
            }
            ImGui::End();
        }
    }
    void Shutdown()
    {
        closing=true; if (ready) Run("xray._shutdown()");
        ready=false; if (python && finalize && initialize) finalize();
        // Keep CPython loaded: extension modules may retain process-lifetime state.
        operators.clear(); panels.clear(); events.clear(); backend=nullptr;
        SDKScriptLua::Shutdown();
    }
}

extern "C" ECORE_API const char* __cdecl SDKScriptCall(const char* operation,const char* text,double x,double y,double z)
{
    static thread_local std::string result;
    try { xrEditorOperationScope recovery; result="{\"ok\":"+SDKScript::Call(operation?operation:"",text?text:"",x,y,z)+"}"; }
    catch (const std::exception& error) { result="{\"error\":"+SDKScript::Quote(error.what())+"}"; }
    catch (...) { result="{\"error\":\"Native editor operation failed\"}"; }
    return result.c_str();
}
