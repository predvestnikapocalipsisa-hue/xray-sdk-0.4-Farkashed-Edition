#pragma once
#include "imgui.h"
#include "XrUIManager.h"
#include "../../Launcher/DiscordSettings.h"
#include <algorithm>
#include <cstring>

namespace FarkashedDiscord
{
    inline void SettingsText(const char* label, std::string& value, size_t capacity = 128)
    {
        char buffer[513] = {};
        const size_t length = (std::min)(value.size(), capacity - 1);
        std::memcpy(buffer, value.data(), length);
        if (ImGui::InputText(label, buffer, capacity)) value = buffer;
    }

    inline void DrawSettings()
    {
        static Settings draft = []() { Settings s; LoadSettings(s); return s; }();
        static std::string message;
        ImGui::Checkbox("Enable Discord RPC", &draft.enabled);
        ImGui::Checkbox("Use custom activity", &draft.custom);
        ImGui::TextDisabled("Application ID (fixed): %s", ApplicationId);
        ImGui::TextWrapped("%s", XrUIManager::GetDiscordStatus());
        ImGui::BeginChild("##discord_fields", ImVec2(0, 340.f * ImGui::GetIO().FontGlobalScale), true);
        ImGui::BeginDisabled(!draft.custom);
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * .58f);
        SettingsText("Activity name", draft.name);
        const int types[] = { 0, 2, 3, 5 };
        int selected = 0;
        for (int i = 0; i < 4; ++i) if (types[i] == draft.type) selected = i;
        if (ImGui::Combo("Activity type", &selected, "Playing\0Listening\0Watching\0Competing\0")) draft.type = types[selected];
        SettingsText("Details", draft.details);
        SettingsText("State", draft.state);
        ImGui::TextWrapped("Name, details and state support {environment} and {document}. Empty fields are hidden.");
        ImGui::Separator();
        SettingsText("Large image", draft.largeImageKey, 513);
        SettingsText("Large tooltip", draft.largeImageText);
        SettingsText("Small image", draft.smallImageKey, 513);
        SettingsText("Small tooltip", draft.smallImageText);
        ImGui::TextWrapped("Images: asset key of this SDK application, or an external HTTPS image URL.");
        ImGui::Separator();
        ImGui::Combo("Timer", &draft.timer, "Hidden\0Elapsed session time\0Unix timestamps (seconds)\0");
        if (draft.timer == 2)
        {
            ImGui::InputScalar("Start (0 = none)", ImGuiDataType_S64, &draft.start);
            ImGui::InputScalar("End (0 = none)", ImGuiDataType_S64, &draft.end);
        }
        ImGui::Separator();
        SettingsText("Button 1 label", draft.buttonLabel[0], 33);
        SettingsText("Button 1 URL", draft.buttonUrl[0], 513);
        SettingsText("Button 2 label", draft.buttonLabel[1], 33);
        SettingsText("Button 2 URL", draft.buttonUrl[1], 513);
        ImGui::TextWrapped("Buttons are visible to other Discord users. Clear both fields to remove a button.");
        if (ImGui::CollapsingHeader("Advanced activity metadata"))
        {
            SettingsText("Party ID", draft.partyId);
            ImGui::InputInt("Party size", &draft.partySize);
            ImGui::InputInt("Party maximum", &draft.partyMax);
            SettingsText("Match secret", draft.matchSecret);
            SettingsText("Join secret", draft.joinSecret);
            SettingsText("Spectate secret", draft.spectateSecret);
            ImGui::Checkbox("Instance", &draft.instance);
            ImGui::TextWrapped("Metadata only: the SDK does not handle game invites or join commands.");
        }
        ImGui::PopItemWidth();
        ImGui::EndDisabled();
        ImGui::EndChild();
        if (ImGui::Button("Apply & Save"))
        {
            message = Validate(draft);
            if (message.empty()) message = SaveSettings(draft) ?
                "Saved. Applied across SDK processes within a second." : "Could not save Discord settings.";
        }
        ImGui::SameLine();
        if (ImGui::Button("Reload"))
            message = LoadSettings(draft) ? "Settings loaded." : "No valid saved settings found.";
        ImGui::SameLine();
        if (ImGui::Button("Defaults")) { draft = Settings(); message = "Defaults restored. Apply to save."; }
        if (!message.empty()) ImGui::TextWrapped("%s", message.c_str());
        ImGui::TextDisabled("Documents / X-Ray SDK / discord.cfg");
    }
}
