#pragma once
#include "imgui.h"
#include "XrUIManager.h"
#include "../../Launcher/DiscordSettings.h"
#include <algorithm>
#include <cstring>

namespace FarkashedDiscord
{
    inline void SettingsText(const char* label, std::string& value, size_t capacity = 128, const char* hint = "")
    {
        char buffer[ImageUrlMaxBytes + 1] = {};
        const size_t length = (std::min)(value.size(), capacity - 1);
        std::memcpy(buffer, value.data(), length);
        if (ImGui::InputTextWithHint(label, hint, buffer, capacity)) value = buffer;
    }

    inline void DrawImageSettings(const char* title, ImageSource& source, std::string& image, std::string& tooltip)
    {
        ImGui::PushID(title);
        int selection = static_cast<int>(source);
        if (ImGui::Combo(title, &selection, "None\0SDK asset key\0External URL (GIF supported)\0"))
        {
            const auto previous = source;
            source = static_cast<ImageSource>(selection);
            if (source != ImageSource::None && previous != source && InferImageSource(image) != source)
                image.clear();
        }
        if (source == ImageSource::AssetKey) SettingsText("Asset key", image, 513, "logo");
        if (source == ImageSource::ExternalUrl)
            SettingsText("Image URL", image, ImageUrlMaxBytes + 1, "https://example.com/image.gif");
        if (source != ImageSource::None) SettingsText("Hover text", tooltip);
        ImGui::PopID();
    }

    inline void DrawSettings()
    {
        static Settings saved = []() { Settings s; LoadSettings(s); return s; }();
        static Settings draft = saved;
        static std::string message;
        static ULONGLONG nextReload = 0;
        // Changing the master switch must not apply any edits to the custom activity.
        const auto now = GetTickCount64();
        if (now >= nextReload)
        {
            nextReload = now + 1000;
            Settings current;
            if (LoadSettings(current) && Serialize(current) != Serialize(saved))
            {
                if (Serialize(draft) == Serialize(saved)) draft = current;
                saved = current;
                draft.enabled = saved.enabled;
            }
        }
        bool enabled = saved.enabled;
        if (ImGui::Checkbox("Enable Discord RPC", &enabled))
        {
            Settings updated = saved;
            LoadSettings(updated);
            updated.enabled = enabled;
            if (SaveSettings(updated))
            {
                if (Serialize(draft) == Serialize(saved)) draft = updated;
                saved = updated;
                draft.enabled = enabled;
                message.clear();
            }
            else
            {
                message = "Could not save the Discord RPC switch.";
            }
        }
        if (ImGui::Checkbox("Use custom activity", &draft.custom)) message.clear();
        ImGui::TextDisabled("Application ID (fixed): %s", ApplicationId);
        ImGui::TextWrapped("%s", XrUIManager::GetDiscordStatus());
        if (ImGui::Button("Apply & Save"))
        {
            message = Validate(draft);
            if (message.empty())
            {
                if (SaveSettings(draft))
                {
                    saved = draft;
                    message.clear();
                }
                else
                {
                    message = "Could not save Discord settings.";
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Reload"))
        {
            if (LoadSettings(saved))
            {
                draft = saved;
                message.clear();
            }
            else
            {
                message = "No valid saved settings found.";
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Defaults"))
        {
            draft = Settings();
            draft.enabled = saved.enabled;
            message.clear();
        }
        if (!message.empty()) ImGui::TextWrapped("%s", message.c_str());
        else if (Serialize(draft) != Serialize(saved)) ImGui::TextDisabled("Unsaved changes. Click Apply & Save.");
        ImGui::BeginChild("##discord_fields", ImVec2(0, 340.f * ImGui::GetIO().FontGlobalScale), true);
        ImGui::BeginDisabled(!draft.custom);
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * .58f);
        SettingsText("Activity name", draft.name);
        ImGui::Checkbox("Show name in Details", &draft.showNameInDetails);
        if (draft.showNameInDetails)
            ImGui::TextWrapped("Uses the name as the Details line and the short status. Discord may keep the registered name in the card header.");
        const int types[] = { 0, 2, 3, 5 };
        int selected = 0;
        for (int i = 0; i < 4; ++i) if (types[i] == draft.type) selected = i;
        if (ImGui::Combo("Activity type", &selected, "Playing\0Listening\0Watching\0Competing\0")) draft.type = types[selected];
        ImGui::BeginDisabled(draft.showNameInDetails);
        SettingsText("Details", draft.details);
        ImGui::EndDisabled();
        SettingsText("State", draft.state);
        ImGui::TextWrapped("Name, details and state support {environment} and {document}. Empty fields are hidden.");
        ImGui::Separator();
        ImGui::TextWrapped("Use a public, direct image URL for animated GIFs.");
        DrawImageSettings("Large image", draft.largeImageSource, draft.largeImageKey, draft.largeImageText);
        DrawImageSettings("Small image", draft.smallImageSource, draft.smallImageKey, draft.smallImageText);
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
    }
}
