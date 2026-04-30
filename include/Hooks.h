#pragma once

#include "Events.h"
#include <mutex>

class StaggerHooks
{
public:
	static void Install()
	{
		SKSE::log::info("Instalando Stagger Hooks...");
		SKSE::AllocTrampoline(64);
		// 1. Hook para StaggerEffect (Magias e Shouts)
		REL::Relocation<std::uintptr_t> staggerEffectVtbl{ RE::VTABLE_StaggerEffect[0] };
		_StaggerEffect_Start = staggerEffectVtbl.write_vfunc(0x14, Hook_StaggerEffect_Start);

		// 2. Hooks para NotifyAnimationGraph
		// Hook para Objetos Animados (Móveis, Portas, etc)
		REL::Relocation<std::uintptr_t> refrVtbl{ RE::VTABLE_TESObjectREFR[3] };
		_NotifyAnimationGraph_REFR = refrVtbl.write_vfunc(0x1, NotifyAnimationGraph_REFR);

		// Hook para NPCs, Criaturas, Montarias (tudo que herda de Character)
		REL::Relocation<std::uintptr_t> charVtbl{ RE::VTABLE_Character[3] };
		_NotifyAnimationGraph_Char = charVtbl.write_vfunc(0x1, NotifyAnimationGraph_Char);

		// Hook para o Player (que tem sua própria VTable que herda de Character)
		REL::Relocation<std::uintptr_t> playerVtbl{ RE::VTABLE_PlayerCharacter[3] };
		_NotifyAnimationGraph_Player = playerVtbl.write_vfunc(0x1, NotifyAnimationGraph_Player);

		SKSE::log::info("Stagger Hooks instalados com sucesso!");
	}

private:
	// --- Hook 1: Magias e Shouts ---
	static void Hook_StaggerEffect_Start(RE::StaggerEffect* a_this)
	{
		if (a_this) {
			auto* targetActor = a_this->GetTargetActor();
			if (targetActor) {
				bool hasImunity = false;
				targetActor->GetGraphVariableBool("hasStaggerImunityCMF", hasImunity);

				// Se estiver imune, aborta o efeito completamente
				if (hasImunity) {
					Sinks::RefreshBlockedStagger(targetActor);
					a_this->Dispel(true); // Cancela o efeito ativo
					return;               // Não chama a função original
				}
			}
		}

		// Se não for imune, segue o jogo normal
		_StaggerEffect_Start(a_this);
	}

	// --- Hooks 2: Eventos de Animação (Melee, Bashes, etc) ---

	static bool NotifyAnimationGraph_REFR(RE::IAnimationGraphManagerHolder* a_this, const RE::BSFixedString& a_eventName)
	{
		if (ShouldBlockStagger(a_this, a_eventName)) return false;
		return _NotifyAnimationGraph_REFR(a_this, a_eventName);
	}

	static bool NotifyAnimationGraph_Char(RE::IAnimationGraphManagerHolder* a_this, const RE::BSFixedString& a_eventName)
	{
		if (ShouldBlockStagger(a_this, a_eventName)) return false;
		return _NotifyAnimationGraph_Char(a_this, a_eventName);
	}

	static bool NotifyAnimationGraph_Player(RE::IAnimationGraphManagerHolder* a_this, const RE::BSFixedString& a_eventName)
	{
		if (ShouldBlockStagger(a_this, a_eventName)) return false;
		return _NotifyAnimationGraph_Player(a_this, a_eventName);
	}

	// Lógica central para decidir se o stagger deve ser bloqueado
	static bool ShouldBlockStagger(RE::IAnimationGraphManagerHolder* a_this, const RE::BSFixedString& a_eventName)
	{
		if (a_eventName.empty()) return false;

		if (a_eventName == "staggerStart") {
			// Fazemos o cast seguro usando a engine do SKSE para pegar a referência base
			auto* refr = skyrim_cast<RE::TESObjectREFR*>(a_this);
			if (refr) {
				auto* actor = refr->As<RE::Actor>();
				if (actor) {
					bool hasImunity = false;
					actor->GetGraphVariableBool("hasStaggerImunityCMF", hasImunity);
					// Se estiver no tracker e for imune, bloqueia!
					if (hasImunity) {
						Sinks::RefreshBlockedStagger(actor);
						return true; // Retorna true informando aos hooks que devem pular a função original
					}
				}
			}
		}

		return false; // Se não for stagger ou não for imune, diz para os hooks continuarem normalmente
	}

	// Ponteiros para as funções originais
	static inline REL::Relocation<decltype(&Hook_StaggerEffect_Start)> _StaggerEffect_Start;
	static inline REL::Relocation<decltype(&NotifyAnimationGraph_REFR)> _NotifyAnimationGraph_REFR;
	static inline REL::Relocation<decltype(&NotifyAnimationGraph_Char)> _NotifyAnimationGraph_Char;
	static inline REL::Relocation<decltype(&NotifyAnimationGraph_Player)> _NotifyAnimationGraph_Player;
};