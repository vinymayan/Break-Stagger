#include "Events.h"
#include <thread>
#include <chrono>
#include <unordered_map>
#include <mutex>
#include "Settings.h"
#include "DelayedDispatcher.h"

void Sinks::RefreshBlockedStagger(RE::Actor* actor) {
	if (!actor || actor->IsDead()) return;

	RE::FormID actorFormID = actor->GetFormID();
	bool isPlayer = actor->IsPlayerRef();
	auto now = std::chrono::steady_clock::now();
	int immunityTimeMs = isPlayer ? StaggerBreakSettings::playerImmunityTimeMs : StaggerBreakSettings::npcImmunityTimeMs;

	{
		std::lock_guard<std::mutex> lock(g_staggerTrackerMutex);
		auto it = g_staggerTracker.find(actorFormID);
		if (it != g_staggerTracker.end()) {
			it->second.lastStaggerTime = now; // Renova o tempo!
		}
		else {
			return;
		}
	}

	// Cria uma nova thread para gerenciar a extensão dessa imunidade
	std::thread([actorFormID, now, immunityTimeMs]() {
		std::this_thread::sleep_for(std::chrono::milliseconds(immunityTimeMs));

		SKSE::GetTaskInterface()->AddTask([actorFormID, now]() {
			std::lock_guard<std::mutex> lock(g_staggerTrackerMutex);
			auto it = g_staggerTracker.find(actorFormID);
			if (it != g_staggerTracker.end()) {
				auto& tracker = it->second;
				// Se o lastStaggerTime for igual, significa que ele não tomou outro hit nesse meio tempo
				if (tracker.lastStaggerTime == now) {
					tracker.count = 0;
					if (tracker.isImmune) {
						tracker.isImmune = false;
						auto targetActor = RE::TESForm::LookupByID<RE::Actor>(actorFormID);
						if (targetActor) {
							targetActor->SetGraphVariableBool("hasStaggerImunityCMF", false);
						}
					}
				}
			}
			});
		}).detach();
}

void Sinks::ScheduleSinkRegistration(RE::Actor* actor, int attempts)
{
	if (attempts > 20) {
		SKSE::log::critical("[Actor3DLoadEventHandler] Desistindo após {} tentativas para o ator {:08X}.", attempts, actor->GetFormID());
		return;
	}

	auto actorHandle = actor->CreateRefHandle();

	Utils::DelayedDispatcher::Get().PostDelayed(std::chrono::milliseconds(100), [actorHandle, attempts]() {
		SKSE::GetTaskInterface()->AddTask([actorHandle, attempts]() {
			if (!actorHandle) return;
			if (!actorHandle.get()) return;

			auto actor = actorHandle.get();

			RE::BSTSmartPointer<RE::BSAnimationGraphManager> graphManager;
			actor->GetAnimationGraphManager(graphManager);

			if (graphManager) {


				if (actor->IsPlayerRef()) {
					actor->RemoveAnimationGraphEventSink(Sinks::NpcCycleSink::GetSingleton());
					if (actor->AddAnimationGraphEventSink(Sinks::NpcCycleSink::GetSingleton())) {
					}

				}
				else {
					Sinks::NpcCombatTracker::UnregisterSink(actor.get());
					Sinks::NpcCombatTracker::RegisterSink(actor.get());
				}
			}
			else {
				// Graph ainda nulo, tenta de novo
				ScheduleSinkRegistration(actor.get(), attempts + 1);
			}
			});
		});
}


RE::BSEventNotifyControl Sinks::NpcCycleSink::ProcessEvent(const RE::BSAnimationGraphEvent* a_event, RE::BSTEventSource<RE::BSAnimationGraphEvent>*)
{
	if (!a_event || !a_event->holder) return RE::BSEventNotifyControl::kContinue;

	auto* actor = a_event->holder->As<RE::Actor>();
	if (!actor || actor->IsDead()) return RE::BSEventNotifyControl::kContinue;

	bool isPlayer = actor->IsPlayerRef();

	// Verifica se o mod está ativado para o ator atual via Settings
	if (isPlayer && !StaggerBreakSettings::playerEnabled) return RE::BSEventNotifyControl::kContinue;
	if (!isPlayer && !StaggerBreakSettings::npcEnabled) return RE::BSEventNotifyControl::kContinue;

	const std::string_view eventName = a_event->tag;
	auto npc = const_cast<RE::Actor*>(actor);

	// Verifica se o evento de animação é o início de um stagger
	if (eventName == "SBF_StaggerStart") {
		if (npc->IsStaggered()) {
			RE::FormID actorFormID = npc->GetFormID();
			auto now = std::chrono::steady_clock::now();

			int baseTimeMs = isPlayer ? StaggerBreakSettings::playerBaseTimeMs : StaggerBreakSettings::npcBaseTimeMs;
			int timeReductionMs = isPlayer ? StaggerBreakSettings::playerTimeReductionMs : StaggerBreakSettings::npcTimeReductionMs;
			int startStaggerCount = isPlayer ? StaggerBreakSettings::playerStartStaggerCount : StaggerBreakSettings::npcStartStaggerCount;
			int resetTimeMs = isPlayer ? StaggerBreakSettings::playerResetTimeMs : StaggerBreakSettings::npcResetTimeMs;
			int immunityTimeMs = isPlayer ? StaggerBreakSettings::playerImmunityTimeMs : StaggerBreakSettings::npcImmunityTimeMs; 
			int currentStaggerCount = 0;
			int waitTimeMs = 0;
			bool triggerAoEPush = false;
			bool sendBreakEvent = false;
			{
				std::lock_guard<std::mutex> lock(g_staggerTrackerMutex);
				auto& tracker = g_staggerTracker[actorFormID];

				// 1. Verifica se passou o tempo de reset (SAÍDA DO ESTADO DE IMUNIDADE)
				if (tracker.count > 0) {
					auto timeSinceLast = std::chrono::duration_cast<std::chrono::milliseconds>(now - tracker.lastStaggerTime).count();
					if (timeSinceLast > resetTimeMs) {
						tracker.count = 0;
						// Se ele estava imune e o contador resetou, ele perde a imunidade
						if (tracker.isImmune) {
							tracker.isImmune = false;
							npc->SetGraphVariableBool("hasStaggerImunityCMF", false);
						}
					}
				}

				// 2. Incrementa o contador
				tracker.count += 1;
				currentStaggerCount = tracker.count;
				tracker.lastStaggerTime = now;

				// 3. Calcula o tempo de espera
				int reductionMultiplier = currentStaggerCount - startStaggerCount;
				if (reductionMultiplier < 0) reductionMultiplier = 0;

				waitTimeMs = baseTimeMs - (reductionMultiplier * timeReductionMs);

				// 4. Verifica se atingiu a imunidade (ENTRADA NO ESTADO DE IMUNIDADE)
				if (waitTimeMs <= 0) {
					waitTimeMs = 0;
					triggerAoEPush = true;

					// Se não estava imune antes, agora está (primeira vez)
					if (!tracker.isImmune) {
						tracker.isImmune = true;
						sendBreakEvent = true; // Marca para disparar o evento fora do lock
						npc->SetGraphVariableBool("hasStaggerImunityCMF", true);
					}
				}
			}

			// Dispara o evento de quebra apenas na primeira vez que entra na imunidade
			if (sendBreakEvent) {
				npc->NotifyAnimationGraph("BreakStaggerCMF");
			}

			std::thread([actorFormID, now, resetTimeMs]() {
				// Espera o tempo exato do reset
				std::this_thread::sleep_for(std::chrono::milliseconds(resetTimeMs));

				// Retorna a execução para a thread principal do SKSE
				SKSE::GetTaskInterface()->AddTask([actorFormID, now]() {
					std::lock_guard<std::mutex> lock(g_staggerTrackerMutex);

					// Busca o tracker atual do ator
					auto it = g_staggerTracker.find(actorFormID);
					if (it != g_staggerTracker.end()) {
						auto& tracker = it->second;

						if (tracker.lastStaggerTime == now) {
							tracker.count = 0;
							if (tracker.isImmune) {
								tracker.isImmune = false;
								auto targetActor = RE::TESForm::LookupByID<RE::Actor>(actorFormID);
								if (targetActor) {
									targetActor->SetGraphVariableBool("hasStaggerImunityCMF", false);
								}
							}
						}
					}
					});
				}).detach();

			std::thread([actorFormID, waitTimeMs, triggerAoEPush]() {
				if (waitTimeMs > 0) {
					std::this_thread::sleep_for(std::chrono::milliseconds(waitTimeMs));
				}

				SKSE::GetTaskInterface()->AddTask([actorFormID, triggerAoEPush]() {
					auto targetActor = RE::TESForm::LookupByID<RE::Actor>(actorFormID);
					if (targetActor && targetActor->IsStaggered()) {
						targetActor->NotifyAnimationGraph("staggerStop");
					}
					});
				}).detach();
		}
	}

	return RE::BSEventNotifyControl::kContinue;
}

RE::BSEventNotifyControl Sinks::NpcCombatTracker::ProcessEvent(const RE::TESCombatEvent* a_event, RE::BSTEventSource<RE::TESCombatEvent>*)
{
	if (!a_event || !a_event->actor) {
		return RE::BSEventNotifyControl::kContinue;
	}

	auto actor = a_event->actor.get();
	auto* npc = actor->As<RE::Actor>();
	if (npc) {
		switch (a_event->newState.get()) {
		case RE::ACTOR_COMBAT_STATE::kCombat:
			NpcCombatTracker::RegisterSink(npc);
			break;
		case RE::ACTOR_COMBAT_STATE::kNone:
			npc->SetGraphVariableInt("StaggerCount", 0);
			npc->SetGraphVariableBool("hasStaggerImunityCMF", false); 
			{
				// Limpa os rastros do ator se ele saiu de combate (libera memória)
				std::lock_guard<std::mutex> lock(g_staggerTrackerMutex);
				g_staggerTracker.erase(npc->GetFormID());
			}
			NpcCombatTracker::UnregisterSink(npc);
			break;
		}
	}
	return RE::BSEventNotifyControl::kContinue;
}

void Sinks::NpcCombatTracker::RegisterSink(RE::Actor* a_actor)
{
	std::unique_lock lock(g_mutex);
	if (g_trackedNPCs.find(a_actor->GetFormID()) == g_trackedNPCs.end()) {
		a_actor->AddAnimationGraphEventSink(&g_npcSink);
		g_trackedNPCs.insert(a_actor->GetFormID());
	}
}

void Sinks::NpcCombatTracker::UnregisterSink(RE::Actor* a_actor)
{
	if (!a_actor || a_actor->IsPlayerRef()) return;

	std::unique_lock lock(g_mutex);
	if (g_trackedNPCs.find(a_actor->GetFormID()) != g_trackedNPCs.end()) {
		a_actor->RemoveAnimationGraphEventSink(&g_npcSink);
		g_trackedNPCs.erase(a_actor->GetFormID());
	}
}

void Sinks::NpcCombatTracker::RegisterSinksForExistingCombatants()
{
	auto* processLists = RE::ProcessLists::GetSingleton();
	if (!processLists) {
		SKSE::log::warn("[NpcCombatTracker] Não foi possível obter ProcessLists.");
		return;
	}

	// Itera sobre todos os atores que estão "ativos" no jogo
	for (auto& actorHandle : processLists->highActorHandles) {
		if (auto actor = actorHandle.get().get()) {
			// A função IsInCombat() nos diz se o ator já está em um estado de combate
			if (!actor->IsPlayerRef()) {
				if (actor->IsInCombat()) {
					SKSE::log::info("[NpcCombatTracker] Ator '{}' ({:08X}) já está em combate. Registrando sink...",
						actor->GetName(), actor->GetFormID());
					// Usamos a mesma função de registro que já existe!
					RegisterSink(actor);
				}
			}

		}
	}

	SKSE::log::info("[NpcCombatTracker] Verificação concluída.");
}

RE::BSEventNotifyControl Sinks::PC3DLoadEventHandler::ProcessEvent(const RE::TESObjectLoadedEvent* a_event, RE::BSTEventSource<RE::TESObjectLoadedEvent>*)
{
	if (!a_event || !a_event->loaded) {
		return RE::BSEventNotifyControl::kContinue;
	}

	// Em vez de pegar o Player Singleton, buscamos o formulário pelo ID do evento
	auto* form = RE::TESForm::LookupByID(a_event->formID);
	if (!form) return RE::BSEventNotifyControl::kContinue;

	// Tentamos converter para Ator. Se não for ator (ex: uma parede), ignoramos.
	auto* actor = form->As<RE::Actor>();

	if (actor) {
		ScheduleSinkRegistration(actor, 0);
	}

	return RE::BSEventNotifyControl::kContinue;
}
