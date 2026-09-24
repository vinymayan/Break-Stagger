#pragma once

#include "SKSEMCP/SKSEMenuFramework.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace StaggerBreakSettings
{
	enum class ValueSourceMode : int
	{
		kFixed = 0,
		kGlobal = 1,
		kActorValue = 2
	};

	enum class CooldownUnit : std::uint8_t
	{
		kSeconds = 0,
		kMinutes = 1,
		kHours = 2
	};

	enum class CooldownClock : std::uint8_t
	{
		kRealTime = 0,
		kGameTime = 1
	};

	struct NumericValueSetting
	{
		int fixedValue = 0;
		RE::FormID global = 0;
		std::string actorValue;
		ValueSourceMode mode = ValueSourceMode::kFixed;
	};

	struct CooldownSetting
	{
		NumericValueSetting duration{ 0 };
		CooldownUnit unit = CooldownUnit::kSeconds;
		CooldownClock clock = CooldownClock::kRealTime;
	};

	struct StaggerProfile
	{
		bool enabled = true;

		NumericValueSetting baseTime{ 2000 };

		NumericValueSetting timeReduction{ 350 };
		RE::FormID timeReductionPerk = 0;

		NumericValueSetting startStaggerCount{ 1 };
		RE::FormID startStaggerCountPerk = 0;

		NumericValueSetting resetTime{ 5000 };

		NumericValueSetting immunityTime{ 3000 };
		RE::FormID immunityPerk = 0;
		CooldownSetting immunityCooldown;

		bool pushEnabled = false;
		bool pushInflictDamage = false;
		RE::FormID pushPerk = 0;
		float pushForce = 350.0f;
		float pushDuration = 0.25f;
		float pushRadius = 600.0f;
	};

	struct StaggerRuntimeProfile
	{
		bool enabled = true;
		int baseTimeMs = 2000;
		int timeReductionMs = 350;
		int startStaggerCount = 1;
		int resetTimeMs = 5000;
		int immunityTimeMs = 3000;
		bool immunityEnabled = true;
		double immunityCooldownSeconds = 0.0;
		CooldownClock immunityCooldownClock = CooldownClock::kRealTime;
		bool pushEnabled = false;
		bool pushInflictDamage = false;
		float pushForce = 350.0f;
		float pushDuration = 0.25f;
		float pushRadius = 600.0f;
	};

	struct StaggerRule
	{
		std::string ruleName = "New Rule";
		RE::FormID perkID = 0;
		StaggerProfile profile;
	};

	inline StaggerProfile player;
	inline StaggerProfile npc;
	inline std::vector<StaggerRule> npcRules;

	void Load();
	void Save();
	void LoadLanguage();
	const char* GetLoc(const std::string& key, const char* fallback);

	StaggerRuntimeProfile ResolveProfile(RE::Actor* actor);
	bool HasPerk(RE::Actor* actor, RE::FormID perkID);

	void PlayerMenu();
	void NPCMenu();
	void RulesMenu();
	void MmRegister();
}
