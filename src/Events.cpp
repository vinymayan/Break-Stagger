#include "Events.h"
#include <algorithm>
#include <thread>
#include <chrono>
#include <unordered_map>
#include <mutex>
#include <cmath>
#include "Settings.h"
#include "DelayedDispatcher.h"
#include "FFC_API.h"
#include "MoreRagdollAPI.h"

namespace
{
	constexpr float kRagdollHoldSeconds = 0.5f;

	MoreRagdollAPI::Interface* GetMoreRagdollAPI()
	{
		const auto module = GetModuleHandleW(L"MoreRagdoll.dll");
		if (!module) {
			return nullptr;
		}
		const auto getAPI = reinterpret_cast<MoreRagdollAPI::GetInterface_t>(
			GetProcAddress(module, "GetMoreRagdollAPI"));
		const auto api = getAPI ? getAPI() : nullptr;
		return api && api->GetVersion() == MoreRagdollAPI::API_VERSION ? api : nullptr;
	}

	void ScheduleRagdollImpulse(RE::ActorHandle targetHandle, float worldX, float worldY,
		float force, bool inflictDamage, int attemptsLeft)
	{
		Utils::DelayedDispatcher::Get().PostDelayed(std::chrono::milliseconds(20),
			[targetHandle, worldX, worldY, force, inflictDamage, attemptsLeft]() {
				SKSE::GetTaskInterface()->AddTask([targetHandle, worldX, worldY, force, inflictDamage, attemptsLeft]() {
					const auto target = targetHandle.get();
					if (!target || target->IsDead() || !target->Is3DLoaded()) return;

					const auto api = FFC_API::_APIV2 ? FFC_API::_APIV2 : FFC_API::RequestAPIV2Direct();
					if (!api) {
						logger::warn("[BreakStagger][Push] FFC v2 API unavailable for target {:08X}.", target->GetFormID());
						return;
					}
					if (target->IsInRagdollState()) {
						if (const auto* controller = target->GetCharController()) {
							const RE::hkVector4 up{ 0.0f, 0.0f, 1.0f, 0.0f };
							const auto forward = controller->forwardVec;
							const auto right = up.Cross(forward);
							const float localX = (right.quad.m128_f32[0] * worldX + right.quad.m128_f32[1] * worldY) * force;
							const float localY = -(forward.quad.m128_f32[0] * worldX + forward.quad.m128_f32[1] * worldY) * force;
							if (api->ApplyRagdollImpulse(target.get(), localX, localY, 0.0f, inflictDamage)) {
								logger::debug("[BreakStagger][Push] FFC v2 ragdoll impulse applied to {:08X}: local=({:.2f}, {:.2f}), z=0, inflictDamage={}.",
									target->GetFormID(), localX, localY, inflictDamage);
								return;
							}
						}
					}
					if (attemptsLeft > 1) {
						ScheduleRagdollImpulse(targetHandle, worldX, worldY, force, inflictDamage, attemptsLeft - 1);
					} else {
						logger::warn("[BreakStagger][Push] FFC v2 did not accept ragdoll impulse for {:08X} within 400 ms.",
							target->GetFormID());
					}
				});
			});
	}

	void PushNearbyEnemies(RE::Actor* source, const StaggerBreakSettings::StaggerRuntimeProfile& profile)
	{
		if (!source) {
			logger::warn("[BreakStagger][Push] Push requested without a source actor.");
			return;
		}
		if (source->IsDead()) {
			logger::debug("[BreakStagger][Push] Source {:08X} is dead; push skipped.", source->GetFormID());
			return;
		}
		if (!profile.pushEnabled) {
			logger::debug(
				"[BreakStagger][Push] Push is disabled for source {:08X}. Check Enable Push and, when selected, the perk requirement.",
				source->GetFormID());
			return;
		}
		if (profile.pushForce <= 0.0f || profile.pushRadius <= 0.0f) {
			logger::warn(
				"[BreakStagger][Push] Invalid settings for source {:08X}: force={}, radius={}; push skipped.",
				source->GetFormID(),
				profile.pushForce,
				profile.pushRadius);
			return;
		}

		auto* ffcAPI = FFC_API::_APIV2 ? FFC_API::_APIV2 : FFC_API::RequestAPIV2Direct();
		if (!ffcAPI) {
			logger::warn("[BreakStagger][Push] ApplyImpulse v2 ragdoll API unavailable; skipping push.");
			return;
		}
		auto* ragdollAPI = GetMoreRagdollAPI();
		if (!ragdollAPI) {
			logger::warn("[BreakStagger][Push] MoreRagdoll API unavailable or version mismatch; skipping push.");
			return;
		}

		auto* processLists = RE::ProcessLists::GetSingleton();
		if (!processLists) {
			logger::warn("[BreakStagger][Push] ProcessLists is unavailable; no actors can be scanned.");
			return;
		}

		const auto sourcePos = source->GetPosition();
		const float radiusSq = profile.pushRadius * profile.pushRadius;
		std::size_t scanned = 0;
		std::size_t invalid = 0;
		std::size_t nonHostile = 0;
		std::size_t outsideRadius = 0;
		std::size_t impulsesQueued = 0;

		logger::debug(
			"[BreakStagger][Push] Starting scan: source={:08X}, api={}, position=({:.2f}, {:.2f}, {:.2f}), force={:.2f}, radius={:.2f}.",
			source->GetFormID(),
			static_cast<const void*>(ffcAPI),
			sourcePos.x,
			sourcePos.y,
			sourcePos.z,
			profile.pushForce,
			profile.pushRadius);

		for (auto& actorHandle : processLists->highActorHandles) {
			++scanned;
			auto* target = actorHandle.get().get();
			if (!target || target == source || target->IsDead() || !target->Is3DLoaded()) {
				++invalid;
				continue;
			}
			if (!target->IsHostileToActor(source) && !source->IsHostileToActor(target)) {
				++nonHostile;
				continue;
			}

			const auto targetPos = target->GetPosition();
			const float dx = targetPos.x - sourcePos.x;
			const float dy = targetPos.y - sourcePos.y;
			const float distSq = dx * dx + dy * dy;
			if (distSq <= 1.0f || distSq > radiusSq) {
				++outsideRadius;
				continue;
			}

			const float invLen = 1.0f / std::sqrt(distSq);
			const float worldX = dx * invLen;
			const float worldY = dy * invLen;

			logger::debug(
				"[BreakStagger][Push] Requesting ragdoll: target={:08X}, distance={:.2f}, worldDir=({:.4f}, {:.4f}), force={:.2f}, hold={:.2f}s.",
				target->GetFormID(),
				std::sqrt(distSq),
				worldX,
				worldY,
				profile.pushForce,
				kRagdollHoldSeconds);
			ragdollAPI->StartRagdoll(target->GetFormID(), kRagdollHoldSeconds, false);
			ScheduleRagdollImpulse(target->GetHandle(), worldX, worldY, profile.pushForce, profile.pushInflictDamage, 20);
			++impulsesQueued;
		}

		logger::debug(
			"[BreakStagger][Push] Scan complete: scanned={}, queued={}, invalid={}, nonHostile={}, outsideRadius={}.",
			scanned,
			impulsesQueued,
			invalid,
			nonHostile,
			outsideRadius);
	}

	double CurrentRealTimeSeconds()
	{
		return std::chrono::duration<double>(
			std::chrono::system_clock::now().time_since_epoch()).count();
	}

	double CurrentGameTimeSeconds()
	{
		constexpr double secondsPerGameDay = 86400.0;
		if (auto* calendar = RE::Calendar::GetSingleton()) {
			return static_cast<double>(calendar->GetCurrentGameTime()) * secondsPerGameDay;
		}
		return -1.0;
	}

	bool IsImmunityCooldownReady(
		RE::FormID actorFormID,
		const StaggerData& tracker,
		const StaggerBreakSettings::StaggerRuntimeProfile& profile)
	{
		if (profile.immunityCooldownSeconds <= 0.0) {
			return true;
		}

		const bool useGameTime =
			profile.immunityCooldownClock == StaggerBreakSettings::CooldownClock::kGameTime;
		const double now = useGameTime ? CurrentGameTimeSeconds() : CurrentRealTimeSeconds();
		const double previous =
			useGameTime ? tracker.lastBreakGameTimeSeconds : tracker.lastBreakRealTimeSeconds;
		if (now < 0.0 || previous < 0.0 || now < previous) {
			return true;
		}

		const double elapsed = now - previous;
		const bool ready = elapsed >= profile.immunityCooldownSeconds;
		logger::debug(
			"[BreakStagger][Cooldown] actor={:08X}, clock={}, elapsed={:.3f}s, required={:.3f}s, remaining={:.3f}s, ready={}.",
			actorFormID,
			useGameTime ? "game" : "real",
			elapsed,
			profile.immunityCooldownSeconds,
			std::max(0.0, profile.immunityCooldownSeconds - elapsed),
			ready);
		return ready;
	}

	void RecordImmunityActivation(StaggerData& tracker)
	{
		tracker.lastBreakRealTimeSeconds = CurrentRealTimeSeconds();
		tracker.lastBreakGameTimeSeconds = CurrentGameTimeSeconds();
	}

	void ScheduleImmunityEnd(
		RE::FormID actorFormID,
		std::uint64_t immunityGeneration,
		int immunityTimeMs)
	{
		std::thread([actorFormID, immunityGeneration, immunityTimeMs]() {
			if (immunityTimeMs > 0) {
				std::this_thread::sleep_for(std::chrono::milliseconds(immunityTimeMs));
			}

			SKSE::GetTaskInterface()->AddTask([actorFormID, immunityGeneration]() {
				std::lock_guard<std::mutex> lock(g_staggerTrackerMutex);
				auto it = g_staggerTracker.find(actorFormID);
				if (it == g_staggerTracker.end()) {
					return;
				}

				auto& tracker = it->second;
				if (!tracker.isImmune || tracker.immunityGeneration != immunityGeneration) {
					return;
				}

				tracker.count = 0;
				tracker.isImmune = false;
				if (auto* actor = RE::TESForm::LookupByID<RE::Actor>(actorFormID)) {
					actor->SetGraphVariableBool("hasStaggerImunityCMF", false);
				}
				logger::debug(
					"[BreakStagger][Immunity] Ended: actor={:08X}, generation={}.",
					actorFormID,
					immunityGeneration);
			});
		}).detach();
	}
}

namespace StaggerBreakSerialization
{
	namespace
	{
		constexpr std::uint32_t cooldownRecord = 'CDN1';
		constexpr std::uint32_t serializationVersion = 1;
	}

	void Save(SKSE::SerializationInterface* serialization)
	{
		if (!serialization || !serialization->OpenRecord(cooldownRecord, serializationVersion)) {
			return;
		}

		std::lock_guard<std::mutex> lock(g_staggerTrackerMutex);
		std::uint32_t count = 0;
		for (const auto& [actorFormID, tracker] : g_staggerTracker) {
			(void)actorFormID;
			if (tracker.lastBreakRealTimeSeconds >= 0.0 ||
				tracker.lastBreakGameTimeSeconds >= 0.0) {
				++count;
			}
		}

		serialization->WriteRecordData(count);
		for (const auto& [actorFormID, tracker] : g_staggerTracker) {
			if (tracker.lastBreakRealTimeSeconds < 0.0 &&
				tracker.lastBreakGameTimeSeconds < 0.0) {
				continue;
			}
			serialization->WriteRecordData(actorFormID);
			serialization->WriteRecordData(tracker.lastBreakRealTimeSeconds);
			serialization->WriteRecordData(tracker.lastBreakGameTimeSeconds);
		}
		logger::debug("[BreakStagger][Serialization] Saved {} immunity cooldown entries.", count);
	}

	void Load(SKSE::SerializationInterface* serialization)
	{
		std::lock_guard<std::mutex> lock(g_staggerTrackerMutex);
		g_staggerTracker.clear();
		if (!serialization) {
			return;
		}

		std::uint32_t type = 0;
		std::uint32_t version = 0;
		std::uint32_t length = 0;
		while (serialization->GetNextRecordInfo(type, version, length)) {
			if (type != cooldownRecord || version != serializationVersion) {
				logger::warn(
					"[BreakStagger][Serialization] Unknown record {:08X} v{}.",
					type,
					version);
				continue;
			}

			std::uint32_t count = 0;
			if (!serialization->ReadRecordData(count)) {
				logger::warn("[BreakStagger][Serialization] Failed to read cooldown entry count.");
				continue;
			}

			std::uint32_t loaded = 0;
			for (std::uint32_t i = 0; i < count; ++i) {
				RE::FormID savedFormID = 0;
				double lastReal = -1.0;
				double lastGame = -1.0;
				if (!serialization->ReadRecordData(savedFormID) ||
					!serialization->ReadRecordData(lastReal) ||
					!serialization->ReadRecordData(lastGame)) {
					logger::warn(
						"[BreakStagger][Serialization] Cooldown record ended at entry {} of {}.",
						i,
						count);
					break;
				}

				RE::FormID resolvedFormID = 0;
				if (!serialization->ResolveFormID(savedFormID, resolvedFormID)) {
					continue;
				}
				auto& tracker = g_staggerTracker[resolvedFormID];
				tracker.lastBreakRealTimeSeconds = lastReal;
				tracker.lastBreakGameTimeSeconds = lastGame;
				++loaded;
			}
			logger::debug(
				"[BreakStagger][Serialization] Loaded {} of {} immunity cooldown entries.",
				loaded,
				count);
		}
	}

	void Revert(SKSE::SerializationInterface*)
	{
		std::lock_guard<std::mutex> lock(g_staggerTrackerMutex);
		g_staggerTracker.clear();
		logger::debug("[BreakStagger][Serialization] Cooldown state reset.");
	}
}

void Sinks::RefreshBlockedStagger(RE::Actor* actor) {
	if (!actor || actor->IsDead()) return;

	RE::FormID actorFormID = actor->GetFormID();
	auto now = std::chrono::steady_clock::now();

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

					Sinks::NpcCombatTracker::UnregisterSink(actor.get());
					Sinks::NpcCombatTracker::RegisterSink(actor.get());

			}
			else {
				// Graph ainda nulo, tenta de novo
				ScheduleSinkRegistration(actor.get(), attempts + 1);
			}
			});
		});
}


void Sinks::HandleStaggerStart(RE::Actor* npc, bool fromNativeEvent)
{
	if (!npc || npc->IsDead()) return;
	const auto profile = StaggerBreakSettings::ResolveProfile(npc);
	if (!profile.enabled) {
		logger::debug("[BreakStagger][Stagger] Ignored actor {:08X}: profile disabled.", npc->GetFormID());
		return;
	}
	if (!fromNativeEvent && !npc->IsStaggered()) {
		logger::debug("[BreakStagger][Stagger] Ignored custom event for actor {:08X}: not staggered.", npc->GetFormID());
		return;
	}
		{
			RE::FormID actorFormID = npc->GetFormID();
			auto now = std::chrono::steady_clock::now();

			int currentStaggerCount = 0;
			int waitTimeMs = 0;
			bool triggerAoEPush = false;
			bool sendBreakEvent = false;
			bool cooldownBlocked = false;
			std::uint64_t immunityGeneration = 0;
			{
				std::lock_guard<std::mutex> lock(g_staggerTrackerMutex);
				auto& tracker = g_staggerTracker[actorFormID];
				if (tracker.count > 0 && now - tracker.lastStaggerTime < std::chrono::milliseconds(500)) {
					logger::debug("[BreakStagger][Stagger] Duplicate start ignored: actor={:08X}, native={}.",
						actorFormID, fromNativeEvent);
					return;
				}

				// 1. Reseta apenas a sequência de contagem após inatividade.
				if (tracker.count > 0) {
					auto timeSinceLast = std::chrono::duration_cast<std::chrono::milliseconds>(now - tracker.lastStaggerTime).count();
					if (timeSinceLast > profile.resetTimeMs) {
						tracker.count = 0;
					}
				}

				// 2. Incrementa o contador
				tracker.count += 1;
				currentStaggerCount = tracker.count;
				tracker.lastStaggerTime = now;
				logger::debug("[BreakStagger][Stagger] Counted: actor={:08X}, native={}, count={}, base={}ms, reduction={}ms, startCount={}.",
					actorFormID, fromNativeEvent, currentStaggerCount, profile.baseTimeMs,
					profile.timeReductionMs, profile.startStaggerCount);

				// 3. Calcula o tempo de espera
				int reductionMultiplier = currentStaggerCount - profile.startStaggerCount;
				if (reductionMultiplier < 0) reductionMultiplier = 0;

				waitTimeMs = profile.baseTimeMs - (reductionMultiplier * profile.timeReductionMs);

				// 4. Verifica se atingiu a imunidade (ENTRADA NO ESTADO DE IMUNIDADE)
				if (waitTimeMs <= 0) {
					waitTimeMs = 0;
					triggerAoEPush = true;
					logger::info(
						"[BreakStagger][Push] Break threshold reached: actor={:08X}, staggerCount={}, pushEnabled={}, force={:.2f}, radius={:.2f}, immunityEnabled={}.",
						npc->GetFormID(),
						currentStaggerCount,
						profile.pushEnabled,
						profile.pushForce,
						profile.pushRadius,
						profile.immunityEnabled);

					// Se não estava imune antes, agora está (primeira vez)
					if (profile.immunityEnabled && !tracker.isImmune) {
						if (IsImmunityCooldownReady(actorFormID, tracker, profile)) {
							tracker.isImmune = true;
							++tracker.immunityGeneration;
							immunityGeneration = tracker.immunityGeneration;
							RecordImmunityActivation(tracker);
							sendBreakEvent = true;
							npc->SetGraphVariableBool("hasStaggerImunityCMF", true);
						} else {
							cooldownBlocked = true;
						}
					}
				}
			}

			// Dispara o evento de quebra apenas na primeira vez que entra na imunidade
			if (sendBreakEvent) {
				const bool eventSent = npc->NotifyAnimationGraph("BreakStaggerCMF");
				logger::debug(
					"[BreakStagger][Immunity] Entered: actor={:08X}, generation={}, duration={}ms, cooldown={:.3f}s, BreakStaggerCMF sent={}.",
					actorFormID,
					immunityGeneration,
					profile.immunityTimeMs,
					profile.immunityCooldownSeconds,
					eventSent);
				ScheduleImmunityEnd(actorFormID, immunityGeneration, profile.immunityTimeMs);
				PushNearbyEnemies(npc, profile);
			}
			else if (triggerAoEPush && !profile.immunityEnabled && profile.pushEnabled) {
				PushNearbyEnemies(npc, profile);
			}
			else if (cooldownBlocked) {
				logger::debug(
					"[BreakStagger][Cooldown] New immunity entry blocked for actor {:08X}; BreakStaggerCMF was not sent.",
					actorFormID);
			}
			else if (triggerAoEPush) {
				logger::debug(
					"[BreakStagger][Immunity] Break threshold reached for actor {:08X}, but no new immunity entry occurred.",
					npc->GetFormID());
			}

			std::thread([actorFormID, now, resetTimeMs = profile.resetTimeMs]() {
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
						}
					}
					});
				}).detach();

			std::thread([actorFormID, waitTimeMs]() {
				if (waitTimeMs > 0) {
					std::this_thread::sleep_for(std::chrono::milliseconds(waitTimeMs));
				}

				SKSE::GetTaskInterface()->AddTask([actorFormID]() {
					auto targetActor = RE::TESForm::LookupByID<RE::Actor>(actorFormID);
					if (targetActor && targetActor->IsStaggered()) {
						targetActor->NotifyAnimationGraph("staggerStop");
					}
					});
				}).detach();
	}
}

RE::BSEventNotifyControl Sinks::NpcCycleSink::ProcessEvent(const RE::BSAnimationGraphEvent* a_event, RE::BSTEventSource<RE::BSAnimationGraphEvent>*)
{
	if (!a_event || !a_event->holder) return RE::BSEventNotifyControl::kContinue;
	auto* actor = a_event->holder->As<RE::Actor>();
	if (!actor || actor->IsDead()) return RE::BSEventNotifyControl::kContinue;
	const std::string_view eventName = a_event->tag;
	if (eventName == "SBF_StaggerStart" || eventName == "staggerStart") {
		HandleStaggerStart(actor, eventName == "staggerStart");
	} else if (eventName == "BreakStaggerCMF") {
		logger::debug(
			"[BreakStagger][Event] BreakStaggerCMF observed from actor {:08X}.",
			actor->GetFormID());
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
				std::lock_guard<std::mutex> lock(g_staggerTrackerMutex);
				const auto actorFormID = npc->GetFormID();
				auto it = g_staggerTracker.find(actorFormID);
				if (it != g_staggerTracker.end()) {
					auto& tracker = it->second;
					tracker.count = 0;
					tracker.isImmune = false;
					++tracker.immunityGeneration;

					// Keep only actors with a cooldown timestamp so it survives combat transitions.
					if (tracker.lastBreakRealTimeSeconds < 0.0 &&
						tracker.lastBreakGameTimeSeconds < 0.0) {
						g_staggerTracker.erase(it);
					}
				}
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
