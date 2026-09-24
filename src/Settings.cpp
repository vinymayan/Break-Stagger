#include "Settings.h"

#include "ListManager.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <cmath>
#include <sstream>
#include <system_error>
#include <unordered_map>

#include <rapidjson/document.h>
#include <rapidjson/filereadstream.h>
#include <rapidjson/filewritestream.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

namespace ImGui = ImGuiMCP;

namespace
{
	constexpr const char* MOD_DIR = "Data/Viny Mods/Break Stagger";
	constexpr const char* SETTINGS_PATH = "Data/Viny Mods/Break Stagger/Settings.json";
	constexpr const char* OLD_SETTINGS_PATH = "Data/SKSE/Plugins/StaggerBreak.json";
	constexpr const char* LANG_PATH = "Data/Viny Mods/Break Stagger/Language.json";
	const std::string RULES_DIR = "Data/Viny Mods/Break Stagger/Rules/";

	static std::unordered_map<std::string, std::string> g_lang;

	std::string ToLower(std::string value)
	{
		std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return value;
	}

	std::string SanitizeRuleFileName(const std::string& ruleName)
	{
		std::string safeName;
		safeName.reserve(ruleName.size());
		for (char ch : ruleName) {
			const auto c = static_cast<unsigned char>(ch);
			safeName.push_back((std::isalnum(c) || ch == ' ' || ch == '_' || ch == '-') ? ch : '_');
		}
		while (!safeName.empty() && (safeName.back() == ' ' || safeName.back() == '.')) {
			safeName.pop_back();
		}
		return safeName.empty() || safeName == "." || safeName == ".." ? "New Rule" : safeName;
	}

	std::string BuildDisplayName(const InternalFormInfo& info)
	{
		std::string label = info.GetDisplayName();
		if (!info.editorID.empty() && info.editorID != label) {
			label += " [" + info.editorID + "]";
		}
		label += " (" + FormUtil::NormalizeFormID(RE::TESForm::LookupByID(info.formID)) + ")";
		return label;
	}

	void ClampProfile(StaggerBreakSettings::StaggerProfile& profile)
	{
		profile.baseTime.fixedValue = std::clamp(profile.baseTime.fixedValue, 100, 60000);
		profile.timeReduction.fixedValue = std::clamp(profile.timeReduction.fixedValue, 0, 60000);
		profile.startStaggerCount.fixedValue = std::clamp(profile.startStaggerCount.fixedValue, 1, 100);
		profile.resetTime.fixedValue = std::clamp(profile.resetTime.fixedValue, 100, 120000);
		profile.immunityTime.fixedValue = std::clamp(profile.immunityTime.fixedValue, 0, 120000);
		profile.immunityCooldown.duration.fixedValue =
			std::clamp(profile.immunityCooldown.duration.fixedValue, 0, 1000000);
		profile.pushForce = std::clamp(profile.pushForce, 0.0f, 5000.0f);
		profile.pushDuration = std::clamp(profile.pushDuration, 0.01f, 5.0f);
		profile.pushRadius = std::clamp(profile.pushRadius, 0.0f, 5000.0f);
	}

	float ReadGlobalValue(RE::FormID formID, float fallback)
	{
		if (!formID) {
			return fallback;
		}
		if (auto global = RE::TESForm::LookupByID<RE::TESGlobal>(formID)) {
			return global->value;
		}
		return fallback;
	}

	float ReadActorValue(RE::Actor* actor, const std::string& actorValueName, float fallback)
	{
		if (!actor || actorValueName.empty()) {
			return fallback;
		}

		const auto actorValue = RE::ActorValueList::LookupActorValueByName(actorValueName.c_str());
		if (actorValue == RE::ActorValue::kNone) {
			logger::warn("[Settings] Actor Value '{}' could not be resolved.", actorValueName);
			return fallback;
		}

		return actor->AsActorValueOwner()->GetActorValue(actorValue);
	}

	int ResolveNumericSetting(RE::Actor* actor, const StaggerBreakSettings::NumericValueSetting& setting, int minValue, int maxValue)
	{
		float value = static_cast<float>(setting.fixedValue);
		switch (setting.mode) {
		case StaggerBreakSettings::ValueSourceMode::kGlobal:
			value = ReadGlobalValue(setting.global, value);
			break;
		case StaggerBreakSettings::ValueSourceMode::kActorValue:
			value = ReadActorValue(actor, setting.actorValue, value);
			break;
		case StaggerBreakSettings::ValueSourceMode::kFixed:
		default:
			break;
		}
		return std::clamp(static_cast<int>(std::round(value)), minValue, maxValue);
	}

	double ResolveCooldownSeconds(
		RE::Actor* actor,
		const StaggerBreakSettings::CooldownSetting& cooldown)
	{
		double duration = static_cast<double>(
			ResolveNumericSetting(actor, cooldown.duration, 0, 1000000));
		if (!std::isfinite(duration) || duration <= 0.0) {
			return 0.0;
		}

		switch (cooldown.unit) {
		case StaggerBreakSettings::CooldownUnit::kMinutes:
			duration *= 60.0;
			break;
		case StaggerBreakSettings::CooldownUnit::kHours:
			duration *= 3600.0;
			break;
		case StaggerBreakSettings::CooldownUnit::kSeconds:
		default:
			break;
		}
		return duration;
	}

	void WriteFormID(rapidjson::Value& parent, rapidjson::Document::AllocatorType& alloc, const char* key, RE::FormID formID)
	{
		std::string value;
		if (auto form = RE::TESForm::LookupByID(formID)) {
			value = FormUtil::NormalizeFormID(form);
		}
		rapidjson::Value jsonValue;
		jsonValue.SetString(value.c_str(), alloc);
		parent.AddMember(rapidjson::Value(key, alloc).Move(), jsonValue, alloc);
	}

	void WriteGlobalRef(rapidjson::Value& parent, rapidjson::Document::AllocatorType& alloc, const char* key, RE::FormID formID)
	{
		rapidjson::Value ref(rapidjson::kObjectType);
		std::string editorID;
		std::string formIDString;

		if (auto global = RE::TESForm::LookupByID<RE::TESGlobal>(formID)) {
			editorID = clib_util::editorID::get_editorID(global);
			formIDString = FormUtil::NormalizeFormID(global);
		}

		rapidjson::Value editorValue;
		editorValue.SetString(editorID.c_str(), alloc);
		ref.AddMember("editorID", editorValue, alloc);

		rapidjson::Value formValue;
		formValue.SetString(formIDString.c_str(), alloc);
		ref.AddMember("form", formValue, alloc);

		parent.AddMember(rapidjson::Value(key, alloc).Move(), ref, alloc);
	}

	void ReadFormID(const rapidjson::Value& parent, const char* key, RE::FormID& formID)
	{
		if (parent.HasMember(key) && parent[key].IsString()) {
			formID = FormUtil::FormIDFromString(parent[key].GetString());
		} else if (parent.HasMember(key) && parent[key].IsUint()) {
			formID = parent[key].GetUint();
		}
	}

	void ReadGlobalRef(const rapidjson::Value& parent, const char* key, RE::FormID& formID)
	{
		if (!parent.HasMember(key)) {
			return;
		}

		const auto& value = parent[key];
		if (value.IsObject()) {
			if (value.HasMember("editorID") && value["editorID"].IsString()) {
				const std::string editorID = value["editorID"].GetString();
				if (!editorID.empty()) {
					if (auto global = RE::TESForm::LookupByEditorID<RE::TESGlobal>(editorID)) {
						formID = global->GetFormID();
						return;
					}
				}
			}

			if (value.HasMember("form") && value["form"].IsString()) {
				try {
					const auto fallbackID = FormUtil::FormIDFromString(value["form"].GetString());
					if (RE::TESForm::LookupByID<RE::TESGlobal>(fallbackID)) {
						formID = fallbackID;
					}
				} catch (...) {
					logger::warn("[Settings] Invalid global form fallback for '{}'.", key);
				}
				return;
			}

			if (value.HasMember("form") && value["form"].IsUint()) {
				const auto fallbackID = value["form"].GetUint();
				if (RE::TESForm::LookupByID<RE::TESGlobal>(fallbackID)) {
					formID = fallbackID;
				}
				return;
			}
		}

		if (value.IsString()) {
			const std::string text = value.GetString();
			if (!text.empty()) {
				if (auto global = RE::TESForm::LookupByEditorID<RE::TESGlobal>(text)) {
					formID = global->GetFormID();
					return;
				}

				try {
					const auto fallbackID = FormUtil::FormIDFromString(text);
					if (RE::TESForm::LookupByID<RE::TESGlobal>(fallbackID)) {
						formID = fallbackID;
					}
				} catch (...) {
					logger::warn("[Settings] Invalid global reference '{}' for '{}'.", text, key);
				}
			}
			return;
		}

		if (value.IsUint()) {
			const auto fallbackID = value.GetUint();
			if (RE::TESForm::LookupByID<RE::TESGlobal>(fallbackID)) {
				formID = fallbackID;
			}
		}
	}

	void WriteNumericSetting(rapidjson::Value& parent, rapidjson::Document::AllocatorType& alloc, const char* key, const StaggerBreakSettings::NumericValueSetting& setting)
	{
		rapidjson::Value value(rapidjson::kObjectType);
		value.AddMember("mode", static_cast<int>(setting.mode), alloc);
		value.AddMember("fixedValue", setting.fixedValue, alloc);
		WriteGlobalRef(value, alloc, "global", setting.global);
		rapidjson::Value actorValue;
		actorValue.SetString(setting.actorValue.c_str(), alloc);
		value.AddMember("actorValue", actorValue, alloc);
		parent.AddMember(rapidjson::Value(key, alloc).Move(), value, alloc);
	}

	void ReadNumericSetting(
		const rapidjson::Value& parent,
		const char* key,
		const char* legacyFixedKey,
		const char* legacyGlobalKey,
		StaggerBreakSettings::NumericValueSetting& setting)
	{
		if (parent.HasMember(key) && parent[key].IsObject()) {
			const auto& value = parent[key];
			if (value.HasMember("mode") && value["mode"].IsInt()) {
				setting.mode = static_cast<StaggerBreakSettings::ValueSourceMode>(std::clamp(value["mode"].GetInt(), 0, 2));
			}
			if (value.HasMember("fixedValue") && value["fixedValue"].IsInt()) {
				setting.fixedValue = value["fixedValue"].GetInt();
			}
			ReadGlobalRef(value, "global", setting.global);
			if (value.HasMember("actorValue") && value["actorValue"].IsString()) {
				setting.actorValue = value["actorValue"].GetString();
			}
			return;
		}

		if (parent.HasMember(legacyFixedKey) && parent[legacyFixedKey].IsInt()) {
			setting.fixedValue = parent[legacyFixedKey].GetInt();
		}
		ReadGlobalRef(parent, legacyGlobalKey, setting.global);
		if (setting.global) {
			setting.mode = StaggerBreakSettings::ValueSourceMode::kGlobal;
		}
	}

	void WriteCooldownSetting(
		rapidjson::Value& parent,
		rapidjson::Document::AllocatorType& alloc,
		const char* key,
		const StaggerBreakSettings::CooldownSetting& cooldown)
	{
		rapidjson::Value value(rapidjson::kObjectType);
		WriteNumericSetting(value, alloc, "duration", cooldown.duration);
		value.AddMember("unit", static_cast<int>(cooldown.unit), alloc);
		value.AddMember("clock", static_cast<int>(cooldown.clock), alloc);
		parent.AddMember(rapidjson::Value(key, alloc).Move(), value, alloc);
	}

	void ReadCooldownSetting(
		const rapidjson::Value& parent,
		const char* key,
		StaggerBreakSettings::CooldownSetting& cooldown)
	{
		if (!parent.HasMember(key) || !parent[key].IsObject()) {
			return;
		}

		const auto& value = parent[key];
		ReadNumericSetting(value, "duration", "durationSeconds", "durationGlobal", cooldown.duration);
		if (value.HasMember("unit") && value["unit"].IsInt()) {
			cooldown.unit = static_cast<StaggerBreakSettings::CooldownUnit>(
				std::clamp(value["unit"].GetInt(), 0, 2));
		}
		if (value.HasMember("clock") && value["clock"].IsInt()) {
			cooldown.clock = static_cast<StaggerBreakSettings::CooldownClock>(
				std::clamp(value["clock"].GetInt(), 0, 1));
		}
	}

	void WriteProfile(rapidjson::Value& value, rapidjson::Document::AllocatorType& alloc, const StaggerBreakSettings::StaggerProfile& profile)
	{
		value.SetObject();
		value.AddMember("enabled", profile.enabled, alloc);
		WriteNumericSetting(value, alloc, "baseTime", profile.baseTime);
		WriteNumericSetting(value, alloc, "timeReduction", profile.timeReduction);
		WriteFormID(value, alloc, "timeReductionPerk", profile.timeReductionPerk);
		WriteNumericSetting(value, alloc, "startStaggerCount", profile.startStaggerCount);
		WriteFormID(value, alloc, "startStaggerCountPerk", profile.startStaggerCountPerk);
		WriteNumericSetting(value, alloc, "resetTime", profile.resetTime);
		WriteNumericSetting(value, alloc, "immunityTime", profile.immunityTime);
		WriteFormID(value, alloc, "immunityPerk", profile.immunityPerk);
		WriteCooldownSetting(value, alloc, "immunityCooldown", profile.immunityCooldown);
		value.AddMember("pushEnabled", profile.pushEnabled, alloc);
		value.AddMember("pushInflictDamage", profile.pushInflictDamage, alloc);
		WriteFormID(value, alloc, "pushPerk", profile.pushPerk);
		value.AddMember("pushForce", profile.pushForce, alloc);
		value.AddMember("pushDuration", profile.pushDuration, alloc);
		value.AddMember("pushRadius", profile.pushRadius, alloc);
	}

	void ReadProfile(const rapidjson::Value& value, StaggerBreakSettings::StaggerProfile& profile)
	{
		if (!value.IsObject()) {
			return;
		}
		if (value.HasMember("enabled") && value["enabled"].IsBool()) profile.enabled = value["enabled"].GetBool();
		ReadNumericSetting(value, "baseTime", "baseTimeMs", "baseTimeGlobal", profile.baseTime);
		ReadNumericSetting(value, "timeReduction", "timeReductionMs", "timeReductionGlobal", profile.timeReduction);
		ReadFormID(value, "timeReductionPerk", profile.timeReductionPerk);
		ReadNumericSetting(value, "startStaggerCount", "startStaggerCount", "startStaggerCountGlobal", profile.startStaggerCount);
		ReadFormID(value, "startStaggerCountPerk", profile.startStaggerCountPerk);
		ReadNumericSetting(value, "resetTime", "resetTimeMs", "resetTimeGlobal", profile.resetTime);
		ReadNumericSetting(value, "immunityTime", "immunityTimeMs", "immunityTimeGlobal", profile.immunityTime);
		ReadFormID(value, "immunityPerk", profile.immunityPerk);
		ReadCooldownSetting(value, "immunityCooldown", profile.immunityCooldown);
		if (value.HasMember("pushEnabled") && value["pushEnabled"].IsBool()) profile.pushEnabled = value["pushEnabled"].GetBool();
		if (value.HasMember("pushInflictDamage") && value["pushInflictDamage"].IsBool()) profile.pushInflictDamage = value["pushInflictDamage"].GetBool();
		ReadFormID(value, "pushPerk", profile.pushPerk);
		if (value.HasMember("pushForce") && value["pushForce"].IsNumber()) profile.pushForce = value["pushForce"].GetFloat();
		if (value.HasMember("pushDuration") && value["pushDuration"].IsNumber()) profile.pushDuration = value["pushDuration"].GetFloat();
		if (value.HasMember("pushRadius") && value["pushRadius"].IsNumber()) profile.pushRadius = value["pushRadius"].GetFloat();
		ClampProfile(profile);
	}

	void ReadLegacySettings(const rapidjson::Document& doc)
	{
		if (!doc.IsObject()) {
			return;
		}
		auto& player = StaggerBreakSettings::player;
		auto& npc = StaggerBreakSettings::npc;
		if (doc.HasMember("playerEnabled") && doc["playerEnabled"].IsBool()) player.enabled = doc["playerEnabled"].GetBool();
		if (doc.HasMember("playerBaseTimeMs") && doc["playerBaseTimeMs"].IsInt()) player.baseTime.fixedValue = doc["playerBaseTimeMs"].GetInt();
		if (doc.HasMember("playerTimeReductionMs") && doc["playerTimeReductionMs"].IsInt()) player.timeReduction.fixedValue = doc["playerTimeReductionMs"].GetInt();
		if (doc.HasMember("playerStartStaggerCount") && doc["playerStartStaggerCount"].IsInt()) player.startStaggerCount.fixedValue = doc["playerStartStaggerCount"].GetInt();
		if (doc.HasMember("playerResetTimeMs") && doc["playerResetTimeMs"].IsInt()) player.resetTime.fixedValue = doc["playerResetTimeMs"].GetInt();
		if (doc.HasMember("playerImmunityTimeMs") && doc["playerImmunityTimeMs"].IsInt()) player.immunityTime.fixedValue = doc["playerImmunityTimeMs"].GetInt();
		if (doc.HasMember("npcEnabled") && doc["npcEnabled"].IsBool()) npc.enabled = doc["npcEnabled"].GetBool();
		if (doc.HasMember("npcBaseTimeMs") && doc["npcBaseTimeMs"].IsInt()) npc.baseTime.fixedValue = doc["npcBaseTimeMs"].GetInt();
		if (doc.HasMember("npcTimeReductionMs") && doc["npcTimeReductionMs"].IsInt()) npc.timeReduction.fixedValue = doc["npcTimeReductionMs"].GetInt();
		if (doc.HasMember("npcStartStaggerCount") && doc["npcStartStaggerCount"].IsInt()) npc.startStaggerCount.fixedValue = doc["npcStartStaggerCount"].GetInt();
		if (doc.HasMember("npcResetTimeMs") && doc["npcResetTimeMs"].IsInt()) npc.resetTime.fixedValue = doc["npcResetTimeMs"].GetInt();
		if (doc.HasMember("npcImmunityTimeMs") && doc["npcImmunityTimeMs"].IsInt()) npc.immunityTime.fixedValue = doc["npcImmunityTimeMs"].GetInt();
		ClampProfile(player);
		ClampProfile(npc);
	}

	bool LoadDocument(const char* path, rapidjson::Document& doc)
	{
		FILE* fp = nullptr;
		fopen_s(&fp, path, "rb");
		if (!fp) {
			return false;
		}
		char readBuffer[65536];
		rapidjson::FileReadStream stream(fp, readBuffer, sizeof(readBuffer));
		doc.ParseStream(stream);
		fclose(fp);
		return !doc.HasParseError() && doc.IsObject();
	}

	bool SaveRule(const StaggerBreakSettings::StaggerRule& rule)
	{
		std::error_code ec;
		std::filesystem::create_directories(RULES_DIR, ec);
		if (ec) {
			logger::error("[Settings] Failed to create rules directory '{}': {}", RULES_DIR, ec.message());
			return false;
		}

		rapidjson::Document doc;
		doc.SetObject();
		auto& alloc = doc.GetAllocator();

		rapidjson::Value ruleName;
		ruleName.SetString(rule.ruleName.c_str(), alloc);
		doc.AddMember("ruleName", ruleName, alloc);
		WriteFormID(doc, alloc, "perk", rule.perkID);

		rapidjson::Value profile;
		WriteProfile(profile, alloc, rule.profile);
		doc.AddMember("profile", profile, alloc);

		const auto path = std::filesystem::path(RULES_DIR) / (SanitizeRuleFileName(rule.ruleName) + ".json");
		std::ofstream file(path, std::ios::binary);
		if (!file.is_open()) {
			logger::error("[Settings] Failed to open rule '{}'.", path.string());
			return false;
		}

		rapidjson::StringBuffer buffer;
		rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
		doc.Accept(writer);
		file << buffer.GetString();
		return file.good();
	}

	void LoadRules()
	{
		StaggerBreakSettings::npcRules.clear();
		std::error_code ec;
		if (!std::filesystem::exists(RULES_DIR, ec)) {
			return;
		}

		for (const auto& entry : std::filesystem::directory_iterator(RULES_DIR, ec)) {
			if (ec) {
				logger::warn("[Settings] Failed to iterate rules '{}': {}", RULES_DIR, ec.message());
				break;
			}
			if (!entry.is_regular_file() || entry.path().extension() != ".json") {
				continue;
			}

			std::ifstream file(entry.path(), std::ios::binary);
			if (!file.is_open()) {
				continue;
			}
			std::stringstream buffer;
			buffer << file.rdbuf();
			rapidjson::Document doc;
			doc.Parse(buffer.str().c_str());
			if (doc.HasParseError() || !doc.IsObject()) {
				logger::warn("[Settings] Invalid rule JSON '{}'.", entry.path().string());
				continue;
			}

			StaggerBreakSettings::StaggerRule rule;
			if (doc.HasMember("ruleName") && doc["ruleName"].IsString()) {
				rule.ruleName = doc["ruleName"].GetString();
			}
			ReadFormID(doc, "perk", rule.perkID);
			if (doc.HasMember("profile") && doc["profile"].IsObject()) {
				ReadProfile(doc["profile"], rule.profile);
			}
			StaggerBreakSettings::npcRules.push_back(rule);
		}
	}

	bool RenderIntSliderWithInput(const char* label, int* value, int minValue, int maxValue)
	{
		bool changed = false;
		ImGui::PushID(label);
		ImGui::SetNextItemWidth(200.0f);
		if (ImGui::SliderInt("##slider", value, minValue, maxValue)) changed = true;
		ImGui::SameLine();
		ImGui::SetNextItemWidth(160.0f);
		if (ImGui::InputInt(label, value)) changed = true;
		*value = std::clamp(*value, minValue, maxValue);
		ImGui::PopID();
		return changed;
	}

	bool RenderFloatSliderWithInput(const char* label, float* value, float minValue, float maxValue, const char* format = "%.2f")
	{
		bool changed = false;
		ImGui::PushID(label);
		ImGui::SetNextItemWidth(200.0f);
		if (ImGui::SliderFloat("##slider", value, minValue, maxValue, format)) changed = true;
		ImGui::SameLine();
		ImGui::SetNextItemWidth(160.0f);
		if (ImGui::InputFloat(label, value, 0.0f, 0.0f, format)) changed = true;
		*value = std::clamp(*value, minValue, maxValue);
		ImGui::PopID();
		return changed;
	}

	bool DrawDropdown(const char* label, const std::string& category, RE::FormID& currentFormID, float width);

	bool RenderStringInput(const char* label, std::string& value)
	{
		char buffer[128]{};
		strcpy_s(buffer, value.c_str());
		if (ImGui::InputText(label, buffer, sizeof(buffer))) {
			value = buffer;
			return true;
		}
		return false;
	}

	bool RenderNumericSourceSetting(const char* label, StaggerBreakSettings::NumericValueSetting& setting, int minValue, int maxValue)
	{
		bool changed = false;
		ImGui::PushID(label);

		const char* modes[] = {
			StaggerBreakSettings::GetLoc("menu.value_source_fixed", "Fixed Value"),
			StaggerBreakSettings::GetLoc("menu.value_source_global", "Global"),
			StaggerBreakSettings::GetLoc("menu.value_source_actor_value", "Actor Value")
		};
		int mode = static_cast<int>(setting.mode);
		if (ImGui::Combo(StaggerBreakSettings::GetLoc("menu.value_source", "Value Source"), &mode, modes, 3)) {
			setting.mode = static_cast<StaggerBreakSettings::ValueSourceMode>(mode);
			changed = true;
		}

		switch (setting.mode) {
		case StaggerBreakSettings::ValueSourceMode::kGlobal:
			if (DrawDropdown(StaggerBreakSettings::GetLoc("menu.value_source_global_form", "Global"), "Global", setting.global, 360.0f)) changed = true;
			break;
		case StaggerBreakSettings::ValueSourceMode::kActorValue:
			if (RenderStringInput(StaggerBreakSettings::GetLoc("menu.actor_value_name", "Actor Value Name"), setting.actorValue)) changed = true;
			break;
		case StaggerBreakSettings::ValueSourceMode::kFixed:
		default:
			if (RenderIntSliderWithInput(label, &setting.fixedValue, minValue, maxValue)) changed = true;
			break;
		}

		ImGui::PopID();
		return changed;
	}

	bool RenderCooldownSetting(
		const char* id,
		StaggerBreakSettings::CooldownSetting& cooldown)
	{
		bool changed = false;
		ImGui::PushID(id);

		const char* clocks[] = {
			StaggerBreakSettings::GetLoc("menu.cooldown_real_time", "Real Time"),
			StaggerBreakSettings::GetLoc("menu.cooldown_game_time", "Game Time")
		};
		int clock = static_cast<int>(cooldown.clock);
		if (ImGui::Combo(
				StaggerBreakSettings::GetLoc("menu.cooldown_clock", "Cooldown Clock"),
				&clock,
				clocks,
				2)) {
			cooldown.clock = static_cast<StaggerBreakSettings::CooldownClock>(clock);
			changed = true;
		}

		const char* units[] = {
			StaggerBreakSettings::GetLoc("menu.cooldown_seconds", "Seconds"),
			StaggerBreakSettings::GetLoc("menu.cooldown_minutes", "Minutes"),
			StaggerBreakSettings::GetLoc("menu.cooldown_hours", "Hours")
		};
		int unit = static_cast<int>(cooldown.unit);
		if (ImGui::Combo(
				StaggerBreakSettings::GetLoc("menu.cooldown_unit", "Cooldown Unit"),
				&unit,
				units,
				3)) {
			cooldown.unit = static_cast<StaggerBreakSettings::CooldownUnit>(unit);
			changed = true;
		}

		if (RenderNumericSourceSetting(
				StaggerBreakSettings::GetLoc("menu.immunity_cooldown_duration", "Immunity Cooldown Duration"),
				cooldown.duration,
				0,
				1000000)) {
			changed = true;
		}

		ImGui::PopID();
		return changed;
	}

	bool DrawDropdown(const char* label, const std::string& category, RE::FormID& currentFormID, float width = -1.0f)
	{
		bool changed = false;
		const auto& fullList = Manager::GetSingleton()->GetList(category);

		std::vector<std::string> displayItems;
		std::vector<const char*> comboItems;
		std::vector<int> mapToFull;
		displayItems.reserve(fullList.size() + 1);
		comboItems.reserve(fullList.size() + 1);
		mapToFull.reserve(fullList.size() + 1);
		displayItems.emplace_back(StaggerBreakSettings::GetLoc("common.none", "None"));
		comboItems.push_back(displayItems.back().c_str());
		mapToFull.push_back(-1);

		int localSelection = 0;
		for (std::size_t i = 0; i < fullList.size(); ++i) {
			displayItems.push_back(BuildDisplayName(fullList[i]));
			comboItems.push_back(displayItems.back().c_str());
			mapToFull.push_back(static_cast<int>(i));
			if (fullList[i].formID == currentFormID) {
				localSelection = static_cast<int>(i) + 1;
			}
		}

		ImGui::PushID(label);
		if (width > 0.0f) {
			ImGui::SetNextItemWidth(width);
		}

		if (ImGui::BeginCombo(label, comboItems[localSelection])) {
			static std::map<std::string, std::string> searchBuffers;
			char searchBuf[256]{};
			if (searchBuffers.contains(label)) {
				strcpy_s(searchBuf, searchBuffers[label].c_str());
			}
			ImGui::SetNextItemWidth(-1.0f);
			if (ImGui::InputText("##filter", searchBuf, sizeof(searchBuf))) {
				searchBuffers[label] = searchBuf;
			}
			ImGui::Separator();
			const auto search = ToLower(searchBuf);
			ImGui::BeginChild("##scroll", ImGui::ImVec2(0, 200), false);
			for (int i = 0; i < static_cast<int>(comboItems.size()); ++i) {
				if (!search.empty() && ToLower(comboItems[i]).find(search) == std::string::npos) {
					continue;
				}
				const bool selected = localSelection == i;
				if (ImGui::Selectable(comboItems[i], selected)) {
					const int originalIndex = mapToFull[i];
					currentFormID = originalIndex < 0 ? 0 : fullList[originalIndex].formID;
					searchBuffers[label].clear();
					changed = true;
				}
				if (selected) {
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndChild();
			ImGui::EndCombo();
		}
		ImGui::PopID();
		return changed;
	}

	void RenderProfile(const char* id, StaggerBreakSettings::StaggerProfile& profile, bool showEnabled, bool& changed)
	{
		ImGui::PushID(id);
		if (showEnabled && ImGui::Checkbox(StaggerBreakSettings::GetLoc("menu.enabled", "Enabled"), &profile.enabled)) changed = true;
		if (RenderNumericSourceSetting(StaggerBreakSettings::GetLoc("menu.base_time", "Base Stagger Time (ms)"), profile.baseTime, 100, 60000)) changed = true;

		ImGui::Separator();
		if (RenderNumericSourceSetting(StaggerBreakSettings::GetLoc("menu.reduction", "Time Reduction per Stagger (ms)"), profile.timeReduction, 0, 60000)) changed = true;
		if (DrawDropdown(StaggerBreakSettings::GetLoc("menu.reduction_perk", "Require Perk for Time Reduction"), "Perk", profile.timeReductionPerk, 360.0f)) changed = true;

		ImGui::Separator();
		if (RenderNumericSourceSetting(StaggerBreakSettings::GetLoc("menu.start_count", "Start Reduction at Stagger Count"), profile.startStaggerCount, 1, 100)) changed = true;
		if (DrawDropdown(StaggerBreakSettings::GetLoc("menu.start_count_perk", "Require Perk for Start Count"), "Perk", profile.startStaggerCountPerk, 360.0f)) changed = true;

		ImGui::Separator();
		if (RenderNumericSourceSetting(StaggerBreakSettings::GetLoc("menu.immunity_time", "Stagger Break Immunity Time (ms)"), profile.immunityTime, 0, 120000)) changed = true;
		if (DrawDropdown(StaggerBreakSettings::GetLoc("menu.immunity_perk", "Require Perk for Stagger Break Immunity"), "Perk", profile.immunityPerk, 360.0f)) changed = true;
		if (RenderCooldownSetting("immunity_cooldown", profile.immunityCooldown)) changed = true;

		ImGui::Separator();
		if (RenderNumericSourceSetting(StaggerBreakSettings::GetLoc("menu.reset_time", "Reset Count After (ms)"), profile.resetTime, 100, 120000)) changed = true;

		ImGui::Separator();
		if (ImGui::Checkbox(StaggerBreakSettings::GetLoc("menu.push_enabled", "Enable Stagger Break Push"), &profile.pushEnabled)) changed = true;
		if (profile.pushEnabled) {
			if (DrawDropdown(StaggerBreakSettings::GetLoc("menu.push_perk", "Require Perk for Push"), "Perk", profile.pushPerk, 360.0f)) changed = true;
			if (ImGui::Checkbox(StaggerBreakSettings::GetLoc("menu.push_inflict_damage", "Inflict Push Damage"), &profile.pushInflictDamage)) changed = true;
			if (RenderFloatSliderWithInput(StaggerBreakSettings::GetLoc("menu.push_force", "Push Force"), &profile.pushForce, 0.0f, 5000.0f, "%.0f")) changed = true;
			if (RenderFloatSliderWithInput(StaggerBreakSettings::GetLoc("menu.push_radius", "Push Radius"), &profile.pushRadius, 0.0f, 5000.0f, "%.0f")) changed = true;
		}
		ClampProfile(profile);
		ImGui::PopID();
	}

}

namespace StaggerBreakSettings
{
	void LoadLanguage()
	{
		g_lang.clear();
		std::ifstream file(LANG_PATH, std::ios::binary);
		if (!file.is_open()) {
			return;
		}
		std::stringstream buffer;
		buffer << file.rdbuf();
		std::string json = buffer.str();
		if (json.size() >= 3 && static_cast<unsigned char>(json[0]) == 0xEF && static_cast<unsigned char>(json[1]) == 0xBB && static_cast<unsigned char>(json[2]) == 0xBF) {
			json.erase(0, 3);
		}
		rapidjson::Document doc;
		doc.Parse(json.c_str());
		if (doc.HasParseError() || !doc.IsObject()) {
			return;
		}
		for (auto itr = doc.MemberBegin(); itr != doc.MemberEnd(); ++itr) {
			if (itr->value.IsObject()) {
				const std::string category = itr->name.GetString();
				for (auto jtr = itr->value.MemberBegin(); jtr != itr->value.MemberEnd(); ++jtr) {
					if (jtr->value.IsString()) {
						g_lang[category + "." + jtr->name.GetString()] = jtr->value.GetString();
					}
				}
			} else if (itr->value.IsString()) {
				g_lang[itr->name.GetString()] = itr->value.GetString();
			}
		}
	}

	const char* GetLoc(const std::string& key, const char* fallback)
	{
		const auto it = g_lang.find(key);
		return it != g_lang.end() ? it->second.c_str() : fallback;
	}

	bool HasPerk(RE::Actor* actor, RE::FormID perkID)
	{
		if (!perkID) {
			return true;
		}
		auto perk = RE::TESForm::LookupByID<RE::BGSPerk>(perkID);
		return actor && perk && actor->HasPerk(perk);
	}

	bool HasRequiredPerk(RE::Actor* actor, RE::FormID perkID)
	{
		if (!perkID) {
			return true;
		}
		auto perk = RE::TESForm::LookupByID<RE::BGSPerk>(perkID);
		return actor && perk && actor->HasPerk(perk);
	}

	StaggerRuntimeProfile ResolveProfile(RE::Actor* actor)
	{
		StaggerProfile source = actor && actor->IsPlayerRef() ? player : npc;
		if (actor && !actor->IsPlayerRef()) {
			for (const auto& rule : npcRules) {
				if (rule.perkID && HasPerk(actor, rule.perkID)) {
					source = rule.profile;
					break;
				}
			}
		}

		ClampProfile(source);
		StaggerRuntimeProfile result;
		result.enabled = source.enabled;
		result.baseTimeMs = ResolveNumericSetting(actor, source.baseTime, 100, 60000);
		result.resetTimeMs = ResolveNumericSetting(actor, source.resetTime, 100, 120000);
		result.immunityEnabled = HasRequiredPerk(actor, source.immunityPerk);
		result.immunityTimeMs = result.immunityEnabled ? ResolveNumericSetting(actor, source.immunityTime, 0, 120000) : 0;
		result.immunityCooldownSeconds =
			result.immunityEnabled ? ResolveCooldownSeconds(actor, source.immunityCooldown) : 0.0;
		result.immunityCooldownClock = source.immunityCooldown.clock;
		result.timeReductionMs = HasRequiredPerk(actor, source.timeReductionPerk) ? ResolveNumericSetting(actor, source.timeReduction, 0, 60000) : 0;
		result.startStaggerCount = HasRequiredPerk(actor, source.startStaggerCountPerk) ? ResolveNumericSetting(actor, source.startStaggerCount, 1, 100) : 100;
		result.pushEnabled = source.pushEnabled && HasRequiredPerk(actor, source.pushPerk);
		result.pushInflictDamage = source.pushInflictDamage;
		result.pushForce = source.pushForce;
		result.pushDuration = source.pushDuration;
		result.pushRadius = source.pushRadius;
		return result;
	}

	void Save()
	{
		std::error_code ec;
		std::filesystem::create_directories(MOD_DIR, ec);
		std::filesystem::create_directories(RULES_DIR, ec);

		rapidjson::Document doc;
		doc.SetObject();
		auto& alloc = doc.GetAllocator();
		rapidjson::Value playerValue;
		WriteProfile(playerValue, alloc, player);
		doc.AddMember("player", playerValue, alloc);
		rapidjson::Value npcValue;
		WriteProfile(npcValue, alloc, npc);
		doc.AddMember("npc", npcValue, alloc);

		FILE* fp = nullptr;
		fopen_s(&fp, SETTINGS_PATH, "wb");
		if (fp) {
			char writeBuffer[65536];
			rapidjson::FileWriteStream stream(fp, writeBuffer, sizeof(writeBuffer));
			rapidjson::Writer<rapidjson::FileWriteStream> writer(stream);
			doc.Accept(writer);
			fclose(fp);
		}

		for (const auto& entry : std::filesystem::directory_iterator(RULES_DIR, ec)) {
			if (ec) {
				break;
			}
			if (entry.is_regular_file() && entry.path().extension() == ".json") {
				std::filesystem::remove(entry.path(), ec);
			}
		}
		for (const auto& rule : npcRules) {
			SaveRule(rule);
		}
	}

	void Load()
	{
		rapidjson::Document doc;
		if (LoadDocument(SETTINGS_PATH, doc)) {
			if (doc.HasMember("player")) ReadProfile(doc["player"], player);
			if (doc.HasMember("npc")) ReadProfile(doc["npc"], npc);
		} else if (LoadDocument(OLD_SETTINGS_PATH, doc)) {
			ReadLegacySettings(doc);
			Save();
		}
		LoadRules();
	}

	void PlayerMenu()
	{
		bool changed = false;
		if (ImGui::CollapsingHeader(GetLoc("menu.player_settings", "Player Stagger Settings"), ImGui::ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::Indent();
			RenderProfile("player", player, true, changed);
			ImGui::Unindent();
		}
		if (changed) Save();
	}

	void NPCMenu()
	{
		bool changed = false;
		if (ImGui::CollapsingHeader(GetLoc("menu.npc_settings", "NPC Stagger Settings"), ImGui::ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::Indent();
			RenderProfile("npc", npc, true, changed);
			ImGui::Unindent();
		}
		if (changed) Save();
	}

	void RulesMenu()
	{
		bool changed = false;
		if (ImGui::Button(GetLoc("menu.add_rule", "+ Add Rule"))) {
			StaggerRule rule;
			rule.ruleName = "New Rule " + std::to_string(npcRules.size() + 1);
			rule.profile = npc;
			npcRules.push_back(rule);
			changed = true;
		}

		for (std::size_t i = 0; i < npcRules.size();) {
			auto& rule = npcRules[i];
			ImGui::PushID(static_cast<int>(i));
			if (ImGui::CollapsingHeader(rule.ruleName.c_str(), ImGui::ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGui::Indent();
				char nameBuf[128]{};
				strcpy_s(nameBuf, rule.ruleName.c_str());
				if (ImGui::InputText(GetLoc("menu.rule_name", "Rule Name"), nameBuf, sizeof(nameBuf))) {
					if (nameBuf[0] != '\0') {
						rule.ruleName = nameBuf;
						changed = true;
					}
				}

				const auto oldPerk = rule.perkID;
				std::string perkLabel = std::string(GetLoc("menu.target_perk", "Target Perk")) + "##" + std::to_string(i);
				if (DrawDropdown(perkLabel.c_str(), "Perk", rule.perkID, 360.0f)) {
					bool conflict = false;
					if (rule.perkID) {
						for (std::size_t j = 0; j < npcRules.size(); ++j) {
							if (i != j && npcRules[j].perkID == rule.perkID) {
								conflict = true;
								break;
							}
						}
					}
					if (conflict) {
						rule.perkID = oldPerk;
					} else {
						changed = true;
					}
				}

				ImGui::Separator();
				RenderProfile("rule_profile", rule.profile, true, changed);
				if (ImGui::Button(GetLoc("menu.remove_rule", "Remove Rule"))) {
					npcRules.erase(npcRules.begin() + static_cast<std::ptrdiff_t>(i));
					changed = true;
					ImGui::Unindent();
					ImGui::PopID();
					continue;
				}
				ImGui::Unindent();
			}
			ImGui::PopID();
			++i;
		}

		if (changed) Save();
	}

	void MmRegister()
	{
		if (!SKSEMenuFramework::IsInstalled()) {
			return;
		}
		LoadLanguage();
		Load();
		SKSEMenuFramework::SetSection("Break Stagger");
		SKSEMenuFramework::AddSectionItem(GetLoc("menu.player_settings", "Player Settings"), PlayerMenu);
		SKSEMenuFramework::AddSectionItem(GetLoc("menu.npc_settings", "NPC Settings"), NPCMenu);
		SKSEMenuFramework::AddSectionItem(GetLoc("menu.rules_settings", "Rules Settings"), RulesMenu);
	}
}
