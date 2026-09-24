#include "logger.h"
#include "hooks.h"
#include "Events.h"
#include "Settings.h"
#include "ListManager.h"
#include "FFC_API.h"
namespace {
    bool hasDFG = false;

    class DynamicFormsGeneratorListener : public RE::BSTEventSink<SKSE::ModCallbackEvent> {
    public:
        static DynamicFormsGeneratorListener* GetSingleton()
        {
            static DynamicFormsGeneratorListener singleton;
            return &singleton;
        }

        void Register()
        {
            if (auto dispatcher = SKSE::GetModCallbackEventSource()) {
                dispatcher->AddEventSink(this);
            }
        }

        RE::BSEventNotifyControl ProcessEvent(const SKSE::ModCallbackEvent* a_event, RE::BSTEventSource<SKSE::ModCallbackEvent>*) override
        {
            if (!a_event) return RE::BSEventNotifyControl::kContinue;

            std::string_view eventName = a_event->eventName.c_str();
            if (eventName == "DynamicFormsGeneratorLoaded") {
                Manager::GetSingleton()->PopulateAllLists();
                return RE::BSEventNotifyControl::kContinue;
            }
            if (eventName == "DynamicFormsGeneratorUpdated") {
                Manager::GetSingleton()->RefreshLists(a_event->strArg.c_str());
                return RE::BSEventNotifyControl::kContinue;
            }

            return RE::BSEventNotifyControl::kContinue;
        }
    };
}

void OnMessage(SKSE::MessagingInterface::Message* message)
{
    FFC_API::ReceiveAPI(message);
    FFC_API::ReceiveAPIV2(message);

    if (message->type == SKSE::MessagingInterface::kPostLoad) {
        hasDFG = GetModuleHandleA("DynamicFormsGenerator.dll") != nullptr;
        if (hasDFG) {
            logger::info("DynamicFormsGenerator.dll found");
        }
    }
    if (message->type == SKSE::MessagingInterface::kDataLoaded) {
        if (!hasDFG) {
            Manager::GetSingleton()->PopulateAllLists();
        }
        auto* ffcAPI = FFC_API::RequestAPIDirect();
        auto* ffcAPIV2 = FFC_API::RequestAPIV2Direct();
        FFC_API::RequestAPI();
        FFC_API::RequestAPIV2();
        logger::info(
            "[BreakStagger][FFC] DataLoaded initialization complete; v1 available={}, v2 ragdoll API available={}.",
            ffcAPI != nullptr, ffcAPIV2 != nullptr);
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

SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
    SetupLog();
    logger::info("Plugin loaded");
    SKSE::Init(skse);
    if (auto* serialization = SKSE::GetSerializationInterface()) {
        serialization->SetUniqueID(StaggerBreakSerialization::kSerializationID);
        serialization->SetSaveCallback(StaggerBreakSerialization::Save);
        serialization->SetLoadCallback(StaggerBreakSerialization::Load);
        serialization->SetRevertCallback(StaggerBreakSerialization::Revert);
    }
    DynamicFormsGeneratorListener::GetSingleton()->Register();
    SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
    return true;
}
