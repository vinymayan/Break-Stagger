#pragma once
#include <shared_mutex>

struct StaggerData {
	int count = 0;
	std::chrono::time_point<std::chrono::steady_clock> lastStaggerTime;
	bool isImmune = false;
};

// Variáveis globais para rastreamento
static std::unordered_map<RE::FormID, StaggerData> g_staggerTracker;
static std::mutex g_staggerTrackerMutex;

namespace Sinks
{
	void RefreshBlockedStagger(RE::Actor* actor);
	void ScheduleSinkRegistration(RE::Actor* actor, int attempts);
	class NpcCycleSink : public RE::BSTEventSink<RE::BSAnimationGraphEvent> {
	public:
		static NpcCycleSink* GetSingleton() {
			static NpcCycleSink singleton;
			return &singleton;
		}

		RE::BSEventNotifyControl ProcessEvent(const RE::BSAnimationGraphEvent* a_event,
			RE::BSTEventSource<RE::BSAnimationGraphEvent>*) override;
	};

	class NpcCombatTracker : public RE::BSTEventSink<RE::TESCombatEvent> {
	public:
		static NpcCombatTracker* GetSingleton() {
			static NpcCombatTracker singleton;
			return &singleton;
		}

		// Função chamada quando um evento de combate ocorre
		RE::BSEventNotifyControl ProcessEvent(const RE::TESCombatEvent* a_event,
			RE::BSTEventSource<RE::TESCombatEvent>*) override;

		static void RegisterSink(RE::Actor* a_actor);
		static void UnregisterSink(RE::Actor* a_actor);

		static void RegisterSinksForExistingCombatants();

	private:
		// Instância compartilhada do nosso processador de lógica
		inline static NpcCycleSink g_npcSink;

		// Guarda os FormIDs dos NPCs que já estamos ouvindo
		inline static std::set<RE::FormID> g_trackedNPCs;
		inline static std::shared_mutex g_mutex;
	};
	class PC3DLoadEventHandler : public RE::BSTEventSink<RE::TESObjectLoadedEvent> {
	public:
		static PC3DLoadEventHandler* GetSingleton() {
			static PC3DLoadEventHandler singleton;
			return &singleton;
		}

		RE::BSEventNotifyControl ProcessEvent(const RE::TESObjectLoadedEvent* a_event, RE::BSTEventSource<RE::TESObjectLoadedEvent>*) override;
	};

};

