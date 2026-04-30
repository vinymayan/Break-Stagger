#pragma once
#include "SKSEMCP/SKSEMenuFramework.hpp"

namespace StaggerBreakSettings
{
    // Player Settings
    inline bool playerEnabled = true;
    inline int playerBaseTimeMs = 2000;
    inline int playerTimeReductionMs = 350;
    inline int playerStartStaggerCount = 1;
    inline int playerResetTimeMs = 5000;
    inline int playerImmunityTimeMs = 3000;
    // NPC Settings
    inline bool npcEnabled = true;
    inline int npcBaseTimeMs = 2000;
    inline int npcTimeReductionMs = 350;
    inline int npcStartStaggerCount = 1;
    inline int npcResetTimeMs = 5000;
    inline int npcImmunityTimeMs = 3000;
    // Funções
    void Load();
    void Save();
    void PlayerMenu();
    void NPCMenu();
    void MmRegister();
}