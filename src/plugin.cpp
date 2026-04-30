#include "logger.h"
#include "hooks.h"
#include "Settings.h"



void OnMessage(SKSE::MessagingInterface::Message* message) {
    if (message->type == SKSE::MessagingInterface::kDataLoaded) {
        StaggerBreakSettings::Load();
        StaggerBreakSettings::MmRegister();
        RE::ScriptEventSourceHolder::GetSingleton()->AddEventSink(Sinks::PC3DLoadEventHandler::GetSingleton());
        StaggerHooks::Install();
    }
    if (message->type == SKSE::MessagingInterface::kNewGame || message->type == SKSE::MessagingInterface::kPostLoadGame) {
        RE::ScriptEventSourceHolder::GetSingleton()->AddEventSink(Sinks::NpcCombatTracker::GetSingleton());
        auto player = RE::PlayerCharacter::GetSingleton();
		Sinks::NpcCombatTracker::RegisterSink(player);
        Sinks::NpcCombatTracker::RegisterSinksForExistingCombatants();
    }
}

SKSEPluginLoad(const SKSE::LoadInterface *skse) {

    SetupLog();
    logger::info("Plugin loaded");
    SKSE::Init(skse);
    SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
    return true;
}
