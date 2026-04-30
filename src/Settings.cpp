#include "Settings.h"
#include <algorithm> // Para std::clamp
#include <cstdio>
 #include <rapidjson/document.h>
 #include <rapidjson/filewritestream.h>
 #include <rapidjson/filereadstream.h>
 #include <rapidjson/writer.h>

const char* StaggerBreakPath = "Data/SKSE/Plugins/StaggerBreak.json";

namespace StaggerBreakSettings {

    void Save() {
        rapidjson::Document doc;
        doc.SetObject();
        auto& allocator = doc.GetAllocator();

        // Player
        doc.AddMember("playerEnabled", playerEnabled, allocator);
        doc.AddMember("playerBaseTimeMs", playerBaseTimeMs, allocator);
        doc.AddMember("playerTimeReductionMs", playerTimeReductionMs, allocator);
        doc.AddMember("playerStartStaggerCount", playerStartStaggerCount, allocator);
        doc.AddMember("playerResetTimeMs", playerResetTimeMs, allocator);
        doc.AddMember("playerImmunityTimeMs", playerImmunityTimeMs, allocator);
        // NPC
        doc.AddMember("npcEnabled", npcEnabled, allocator);
        doc.AddMember("npcBaseTimeMs", npcBaseTimeMs, allocator);
        doc.AddMember("npcTimeReductionMs", npcTimeReductionMs, allocator);
        doc.AddMember("npcStartStaggerCount", npcStartStaggerCount, allocator);
        doc.AddMember("npcResetTimeMs", npcResetTimeMs, allocator);
        doc.AddMember("npcImmunityTimeMs", npcImmunityTimeMs, allocator);
        FILE* fp = nullptr;
        fopen_s(&fp, StaggerBreakPath, "wb");
        if (fp) {
            char writeBuffer[65536];
            rapidjson::FileWriteStream os(fp, writeBuffer, sizeof(writeBuffer));
            rapidjson::Writer<rapidjson::FileWriteStream> writer(os);
            doc.Accept(writer);
            fclose(fp);
        }
    }

    void Load() {
        FILE* fp = nullptr;
        fopen_s(&fp, StaggerBreakPath, "rb");
        if (fp) {
            char readBuffer[65536];
            rapidjson::FileReadStream is(fp, readBuffer, sizeof(readBuffer));
            rapidjson::Document doc;
            doc.ParseStream(is);
            fclose(fp);

            if (doc.IsObject()) {
                // Player
                if (doc.HasMember("playerEnabled")) playerEnabled = doc["playerEnabled"].GetBool();
                if (doc.HasMember("playerBaseTimeMs")) playerBaseTimeMs = doc["playerBaseTimeMs"].GetInt();
                if (doc.HasMember("playerTimeReductionMs")) playerTimeReductionMs = doc["playerTimeReductionMs"].GetInt();
                if (doc.HasMember("playerStartStaggerCount")) playerStartStaggerCount = doc["playerStartStaggerCount"].GetInt();
                if (doc.HasMember("playerResetTimeMs")) playerResetTimeMs = doc["playerResetTimeMs"].GetInt();
                if (doc.HasMember("playerImmunityTimeMs")) playerImmunityTimeMs = doc["playerImmunityTimeMs"].GetInt();
                // NPC
                if (doc.HasMember("npcEnabled")) npcEnabled = doc["npcEnabled"].GetBool();
                if (doc.HasMember("npcBaseTimeMs")) npcBaseTimeMs = doc["npcBaseTimeMs"].GetInt();
                if (doc.HasMember("npcTimeReductionMs")) npcTimeReductionMs = doc["npcTimeReductionMs"].GetInt();
                if (doc.HasMember("npcStartStaggerCount")) npcStartStaggerCount = doc["npcStartStaggerCount"].GetInt();
                if (doc.HasMember("npcResetTimeMs")) npcResetTimeMs = doc["npcResetTimeMs"].GetInt();
                if (doc.HasMember("npcImmunityTimeMs")) npcImmunityTimeMs = doc["npcImmunityTimeMs"].GetInt();
            }
        }
    }

    void PlayerMenu() {
        bool changed = false;

        if (ImGuiMCP::CollapsingHeader("Player Stagger Settings", ImGuiMCP::ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGuiMCP::Checkbox("Enable Mod for Player", &playerEnabled)) changed = true;

            if (playerEnabled) {
                ImGuiMCP::Indent();
                ImGuiMCP::Spacing();

                // Tempo Base
                ImGuiMCP::SetNextItemWidth(200.0f);
                if (ImGuiMCP::SliderInt("Base Stagger Time (ms)", &playerBaseTimeMs, 500, 10000)) changed = true;
                ImGuiMCP::SameLine();
                ImGuiMCP::SetNextItemWidth(100.0f);
                if (ImGuiMCP::InputInt("##PBasePrecise", &playerBaseTimeMs, 0, 0)) {
                    playerBaseTimeMs = std::clamp(playerBaseTimeMs, 100, 20000);
                    changed = true;
                }

                // Redução de Tempo por Stagger
                ImGuiMCP::SetNextItemWidth(200.0f);
                if (ImGuiMCP::SliderInt("Time Reduction per Stagger (ms)", &playerTimeReductionMs, 0, 2000)) changed = true;
                ImGuiMCP::SameLine();
                ImGuiMCP::SetNextItemWidth(100.0f);
                if (ImGuiMCP::InputInt("##PRedPrecise", &playerTimeReductionMs, 0, 0)) {
                    playerTimeReductionMs = std::clamp(playerTimeReductionMs, 0, 5000);
                    changed = true;
                }

                // Iniciar redução a partir de qual Stagger
                ImGuiMCP::SetNextItemWidth(200.0f);
                if (ImGuiMCP::SliderInt("Start Reduction at Stagger Count", &playerStartStaggerCount, 1, 10)) changed = true;
                ImGuiMCP::SameLine();
                ImGuiMCP::SetNextItemWidth(100.0f);
                if (ImGuiMCP::InputInt("##PStartPrecise", &playerStartStaggerCount, 0, 0)) {
                    playerStartStaggerCount = std::clamp(playerStartStaggerCount, 1, 100);
                    changed = true;
                }

                ImGuiMCP::Spacing();
                ImGuiMCP::Separator();
                ImGuiMCP::Spacing();

                // Tempo de Imunidade
                ImGuiMCP::TextDisabled("Stagger Immunity Duration");
                ImGuiMCP::SetNextItemWidth(200.0f);
                if (ImGuiMCP::SliderInt("Immunity Time (ms)##Player", &playerImmunityTimeMs, 500, 30000)) changed = true;
                ImGuiMCP::SameLine();
                ImGuiMCP::SetNextItemWidth(100.0f);
                if (ImGuiMCP::InputInt("##PImmunityPrecise", &playerImmunityTimeMs, 0, 0)) {
                    playerImmunityTimeMs = std::clamp(playerImmunityTimeMs, 500, 60000);
                    changed = true;
                }

                ImGuiMCP::Spacing();
                ImGuiMCP::Separator();
                ImGuiMCP::Spacing();

                // Tempo de Reset do Contador
                ImGuiMCP::TextDisabled("Stagger Counter Reset");
                ImGuiMCP::SetNextItemWidth(200.0f);
                if (ImGuiMCP::SliderInt("Reset Count After (ms)", &playerResetTimeMs, 1000, 30000)) changed = true;
                ImGuiMCP::SameLine();
                ImGuiMCP::SetNextItemWidth(100.0f);
                if (ImGuiMCP::InputInt("##PResetPrecise", &playerResetTimeMs, 0, 0)) {
                    playerResetTimeMs = std::clamp(playerResetTimeMs, 500, 60000);
                    changed = true;
                }

                ImGuiMCP::Unindent();
            }
        }

        if (changed) Save();
    }

    void NPCMenu() {
        bool changed = false;

        if (ImGuiMCP::CollapsingHeader("NPC Stagger Settings", ImGuiMCP::ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGuiMCP::Checkbox("Enable Mod for NPCs", &npcEnabled)) changed = true;

            if (npcEnabled) {
                ImGuiMCP::Indent();
                ImGuiMCP::Spacing();

                // Tempo Base
                ImGuiMCP::SetNextItemWidth(200.0f);
                if (ImGuiMCP::SliderInt("Base Stagger Time (ms)", &npcBaseTimeMs, 500, 10000)) changed = true;
                ImGuiMCP::SameLine();
                ImGuiMCP::SetNextItemWidth(100.0f);
                if (ImGuiMCP::InputInt("##NBasePrecise", &npcBaseTimeMs, 0, 0)) {
                    npcBaseTimeMs = std::clamp(npcBaseTimeMs, 100, 20000);
                    changed = true;
                }

                // Redução de Tempo por Stagger
                ImGuiMCP::SetNextItemWidth(200.0f);
                if (ImGuiMCP::SliderInt("Time Reduction per Stagger (ms)", &npcTimeReductionMs, 0, 2000)) changed = true;
                ImGuiMCP::SameLine();
                ImGuiMCP::SetNextItemWidth(100.0f);
                if (ImGuiMCP::InputInt("##NRedPrecise", &npcTimeReductionMs, 0, 0)) {
                    npcTimeReductionMs = std::clamp(npcTimeReductionMs, 0, 5000);
                    changed = true;
                }

                // Iniciar redução a partir de qual Stagger
                ImGuiMCP::SetNextItemWidth(200.0f);
                if (ImGuiMCP::SliderInt("Start Reduction at Stagger Count", &npcStartStaggerCount, 1, 10)) changed = true;
                ImGuiMCP::SameLine();
                ImGuiMCP::SetNextItemWidth(100.0f);
                if (ImGuiMCP::InputInt("##NStartPrecise", &npcStartStaggerCount, 0, 0)) {
                    npcStartStaggerCount = std::clamp(npcStartStaggerCount, 1, 100);
                    changed = true;
                }

                ImGuiMCP::Spacing();
                ImGuiMCP::Separator();
                ImGuiMCP::Spacing();

                ImGuiMCP::TextDisabled("Stagger Immunity Duration");
                ImGuiMCP::SetNextItemWidth(200.0f);
                if (ImGuiMCP::SliderInt("Immunity Time (ms)##NPC", &npcImmunityTimeMs, 500, 30000)) changed = true;
                ImGuiMCP::SameLine();
                ImGuiMCP::SetNextItemWidth(100.0f);
                if (ImGuiMCP::InputInt("##NImmunityPrecise", &npcImmunityTimeMs, 0, 0)) {
                    npcImmunityTimeMs = std::clamp(npcImmunityTimeMs, 500, 60000);
                    changed = true;
                }

                ImGuiMCP::Spacing();
                ImGuiMCP::Separator();
                ImGuiMCP::Spacing();

                // Tempo de Reset do Contador
                ImGuiMCP::TextDisabled("Stagger Counter Reset");
                ImGuiMCP::SetNextItemWidth(200.0f);
                if (ImGuiMCP::SliderInt("Reset Count After (ms)", &npcResetTimeMs, 1000, 30000)) changed = true;
                ImGuiMCP::SameLine();
                ImGuiMCP::SetNextItemWidth(100.0f);
                if (ImGuiMCP::InputInt("##NResetPrecise", &npcResetTimeMs, 0, 0)) {
                    npcResetTimeMs = std::clamp(npcResetTimeMs, 500, 60000);
                    changed = true;
                }

                ImGuiMCP::Unindent();
            }
        }

        if (changed) Save();
    }

    void MmRegister() {
        if (SKSEMenuFramework::IsInstalled()) {
            SKSEMenuFramework::SetSection("Break Stagger");
            SKSEMenuFramework::AddSectionItem("Player Settings", PlayerMenu);
            SKSEMenuFramework::AddSectionItem("NPC Settings", NPCMenu);
        }
    }
}