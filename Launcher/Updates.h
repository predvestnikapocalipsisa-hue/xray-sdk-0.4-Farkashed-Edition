#pragma once
#include "UpdateBridge.h"
#include "../LauncherAssets/ResourceIds.h"
#include "imgui.h"
#include <fstream>
#include <sstream>
#include <cmath>

namespace SDKUpdates
{
    class Panel
    {
        std::wstring target, directory, script, state;
        std::string localVersion, phase, message;
        HANDLE process = nullptr;
        bool requested = false;
        float progress = 0.f, rate = 0.f;
        double nextPoll = 0;

        void Start(bool install)
        {
            if (process) { CloseHandle(process); process = nullptr; }
            HMODULE binary = LoadLibraryExW((target + L"\\Launcher.exe").c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE);
            HRSRC info = binary ? FindResourceW(binary, MAKEINTRESOURCEW(IDR_SDK_BUILD_INFO), RT_RCDATA) : nullptr;
            HGLOBAL loaded = info ? LoadResource(binary, info) : nullptr;
            const char* bytes = loaded ? static_cast<const char*>(LockResource(loaded)) : nullptr;
            if (bytes) { std::istringstream stream(std::string(bytes, SizeofResource(binary, info))); std::getline(stream, localVersion); }
            if (binary) FreeLibrary(binary);
            if (!localVersion.empty() && localVersion.back() == '\r') localVersion.pop_back();
            if (localVersion.empty()) { phase = "error"; message = "Installed SDK has no embedded version information."; return; }
            if (directory.empty()) {
                wchar_t temp[MAX_PATH] = {}, unique[MAX_PATH] = {};
                if (!GetTempPathW(MAX_PATH, temp) || !GetTempFileNameW(temp, L"xru", 0, unique)) { phase="error"; message="Cannot create updater workspace."; return; }
                DeleteFileW(unique); directory = unique;
                if (!CreateDirectoryW(directory.c_str(), nullptr)) { phase="error"; message="Cannot create updater workspace."; return; }
            }
            script = directory + L"\\Update.ps1"; state = directory + L"\\state.txt";
            DeleteFileW(state.c_str());
            HMODULE module = GetModuleHandleW(nullptr);
            HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(IDR_SDK_UPDATE_SCRIPT), RT_RCDATA);
            HGLOBAL data = resource ? LoadResource(module, resource) : nullptr;
            const void* contents = data ? LockResource(data) : nullptr;
            if (!contents) { phase="error"; message="Updater script resource is missing."; return; }
            std::ofstream output(script, std::ios::binary);
            output.write(static_cast<const char*>(contents), SizeofResource(module,resource)); output.close();
            if (!output) { phase="error"; message="Cannot write updater script."; return; }
            wchar_t system[MAX_PATH] = {}; GetSystemDirectoryW(system, MAX_PATH);
            const std::wstring executable = std::wstring(system) + L"\\WindowsPowerShell\\v1.0\\powershell.exe";
            std::wstring version(localVersion.begin(),localVersion.end());
            // Build info is trusted, but never allow it to break argument quoting.
            if (version.find_first_of(L"\"\r\n") != std::wstring::npos) { phase="error"; message="Invalid local version."; return; }
            std::wstring command = L"\"" + executable + L"\" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"" + script +
                L"\" -Target \"" + target + L"\" -LocalVersion \"" + version + L"\" -State \"" + state + L"\"" + (install ? L" -Install" : L"");
            STARTUPINFOW startup = {}; startup.cb = sizeof(startup); PROCESS_INFORMATION result = {};
            if (!CreateProcessW(executable.c_str(), &command[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                directory.c_str(), &startup, &result)) { phase="error"; message="Cannot start update worker."; return; }
            CloseHandle(result.hThread); process = result.hProcess;
            phase="checking"; message="Checking GitHub releases..."; progress=rate=0;
        }

    public:
        ~Panel() { if (process) CloseHandle(process); }
        const std::wstring& Target() const { return target; }
        bool InitializeFromCommandLine()
        {
            int count=0; auto args=CommandLineToArgvW(GetCommandLineW(), &count);
            bool check=false;
            if (args) {
                for (int i=1;i<count;++i) {
                    if (wcscmp(args[i],L"--check-updates")==0) check=true;
                    if (wcscmp(args[i],L"--update-target")==0 && i+1<count) target=FarkashedLauncher::FullPath(args[++i]);
                }
                LocalFree(args);
            }
            if (check && !target.empty()) { requested=true; Start(false); return true; }
            return false;
        }
        bool Busy() const { return phase=="checking" || phase=="downloading" || phase=="extracting" || phase=="waiting" || phase=="installing"; }
        void Poll()
        {
            if (!process || ImGui::GetTime()<nextPoll) return;
            nextPoll=ImGui::GetTime()+.1;
            std::ifstream input(state, std::ios::binary);
            std::string newPhase, newMessage;
            float newProgress=0, newRate=0;
            if (std::getline(input,newPhase) && std::getline(input,newMessage) && input>>newProgress>>newRate) {
                phase=newPhase; message=newMessage; progress=newProgress; rate=newRate;
            }
            if (WaitForSingleObject(process,0)==WAIT_OBJECT_0) {
                if (Busy()) { phase="error"; message="Update worker stopped unexpectedly. Try again."; }
                CloseHandle(process); process=nullptr;
            }
        }
        void Draw(float scale)
        {
            if (requested) { ImGui::OpenPopup("SDK updates"); requested=false; }
            ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),ImGuiCond_Always,ImVec2(.5f,.5f));
            ImGui::SetNextWindowSize(ImVec2(480.f*scale,250.f*scale),ImGuiCond_Always);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize,0.f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,12.f*scale);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(20.f*scale,16.f*scale));
            ImGui::PushStyleColor(ImGuiCol_PopupBg,ImVec4(.035f,.035f,.04f,1.f));
            ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg,ImVec4(0.f,0.f,0.f,.45f));
            const bool visible=ImGui::BeginPopupModal("SDK updates",nullptr,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(3);
            if (!visible) return;
            const bool busy=Busy();
            ImGui::TextColored(ImGui::GetStyle().Colors[ImGuiCol_CheckMark],"X-RAY SDK");
            ImGui::TextUnformatted("SDK updates");
            ImGui::Spacing();
            // Keep messages scrollable without changing the dialog or footer size.
            ImGui::BeginChild("##UpdateMessage",ImVec2(0,90.f*scale),false);
            ImGui::PushTextWrapPos(0);
            if (phase=="error") ImGui::TextColored(ImVec4(1.f,.27f,.32f,1.f),"Update failed");
            ImGui::TextUnformatted(message.c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndChild();
            if (busy)
            {
                ImGui::SetCursorPosY(ImGui::GetWindowHeight()-68.f*scale);
                const char* label=phase=="checking" ? "Checking releases" : phase=="extracting" ? "Preparing files" :
                    phase=="waiting" ? "Waiting for SDK to close" : phase=="installing" ? "Installing" : "Downloading";
                ImGui::TextDisabled("%s",label);
                const bool measured=phase=="downloading" || phase=="installing";
                const ImVec2 min=ImGui::GetCursorScreenPos();
                const float width=ImGui::GetContentRegionAvail().x, height=6.f*scale;
                ImDrawList* draw=ImGui::GetWindowDrawList();
                draw->AddRectFilled(min,ImVec2(min.x+width,min.y+height),IM_COL32(35,35,39,255),height*.5f);
                if (measured)
                {
                    const float fraction=(std::max)(0.f,(std::min)(1.f,progress));
                    if (fraction>0.f) draw->AddRectFilled(min,ImVec2(min.x+width*fraction,min.y+height),
                        ImGui::GetColorU32(ImGuiCol_CheckMark),height*.5f);
                }
                else
                {
                    // Checking, extraction and lock waits have no byte total.
                    // Show motion instead of an empty zero-percent bar.
                    const float travel=float(std::fmod(ImGui::GetTime()*.65,1.0));
                    const float left=(std::max)(0.f,travel*1.3f-.3f);
                    const float right=(std::min)(1.f,travel*1.3f);
                    draw->AddRectFilled(ImVec2(min.x+width*left,min.y),ImVec2(min.x+width*right,min.y+height),
                        ImGui::GetColorU32(ImGuiCol_CheckMark),height*.5f);
                }
                ImGui::Dummy(ImVec2(width,height));
                if (measured) ImGui::TextDisabled("%.0f%%  /  %.2f MB/s",100.f*(std::max)(0.f,(std::min)(1.f,progress)),rate/1048576.f);
            }
            else
            {
                ImGui::SetCursorPosY(ImGui::GetWindowHeight()-16.f*scale-ImGui::GetFrameHeight());
                if (phase=="available")
                {
                    if (ImGui::Button("Download and install",ImVec2(240.f*scale,0))) Start(true);
                    ImGui::SameLine();
                }
                else if (phase=="error")
                {
                    if (ImGui::Button("Retry",ImVec2(120.f*scale,0))) Start(false);
                    ImGui::SameLine();
                }
                else if (phase=="done")
                {
                    if (ImGui::Button("Open updated SDK",ImVec2(240.f*scale,0)))
                        ShellExecuteW(nullptr,L"open",(target+L"\\Launcher.exe").c_str(),nullptr,target.c_str(),SW_SHOWNORMAL);
                    ImGui::SameLine();
                }
                else ImGui::SetCursorPosX((ImGui::GetWindowWidth()-100.f*scale)*.5f);
                if (ImGui::Button("OK",ImVec2(100.f*scale,0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    };
}
