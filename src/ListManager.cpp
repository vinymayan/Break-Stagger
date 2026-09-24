#include "ListManager.h"

namespace FormUtil {
    const RE::TESFile* GetMasterFile(RE::TESForm* ref) {
        if (!ref) return nullptr;

        uint32_t formID = ref->GetFormID();
        uint8_t modIndex = static_cast<uint8_t>(formID >> 24);

        auto dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) return nullptr;

        if (modIndex == 0xFE) {
            uint16_t eslIndex = (formID >> 12) & 0xFFF;
            return dataHandler->LookupLoadedLightModByIndex(eslIndex);
        }

        return dataHandler->LookupLoadedModByIndex(modIndex);
    }

    std::string NormalizeFormID(RE::TESForm* form) {
        if (!form) return {};

        RE::FormID formID = form->GetFormID();
        uint8_t modIndex = (formID >> 24) & 0xFF;

        if (modIndex == 0xFF) {
            return std::format("{:X}", formID);
        }

        auto file = GetMasterFile(form);
        if (!file) return std::format("{:X}", formID);

        uint32_t localID = formID & 0x00FFFFFF;

        if (modIndex == 0xFE) {
            uint32_t eslID = localID & 0xFFF;
            return std::format("{}|{:X}", file->GetFilename(), eslID);
        }

        return std::format("{}|{:X}", file->GetFilename(), localID);
    }

    RE::FormID FormIDFromString(const std::string& str) {
        auto pos = str.find('|');
        if (pos != std::string::npos) {
            std::string plugin = str.substr(0, pos);
            std::string idStr = str.substr(pos + 1);
            RE::FormID localId = std::stoul(idStr, nullptr, 16);
            auto dataHandler = RE::TESDataHandler::GetSingleton();
            return dataHandler ? dataHandler->LookupFormID(localId, plugin) : 0;
        }
        return str.empty() ? 0 : std::stoul(str, nullptr, 16);
    }
}

namespace {
    bool IsValidUTF8(const std::string& string) {
        int c, i, ix, n, j;
        for (i = 0, ix = static_cast<int>(string.length()); i < ix; i++) {
            c = static_cast<unsigned char>(string[i]);
            if (c <= 0x7f) n = 0;
            else if ((c & 0xE0) == 0xC0) n = 1;
            else if (c == 0xED && i < (ix - 1) && (static_cast<unsigned char>(string[i + 1]) & 0xA0) == 0xA0) return false;
            else if ((c & 0xF0) == 0xE0) n = 2;
            else if ((c & 0xF8) == 0xF0) n = 3;
            else return false;

            for (j = 0; j < n && i < ix; j++) {
                if ((++i == ix) || ((static_cast<unsigned char>(string[i]) & 0xC0) != 0x80)) {
                    return false;
                }
            }
        }
        return true;
    }

    bool IsValidPerk(RE::BGSPerk* perk) {
        if (!perk) return false;
        if ((perk->formFlags & RE::BGSPerk::RecordFlags::kNonPlayable) != 0) return false;
        if (!perk->data.playable) return false;
        if (perk->data.hidden) return false;
        if (perk->data.trait) return false;
        if (perk->fullName.empty()) return false;

        RE::BSString descStr;
        perk->TESDescription::GetDescription(descStr, perk);
        return !descStr.empty();
    }
}

void Manager::PopulateAllLists(bool forceRefresh) {
    if (_isPopulated && !forceRefresh) return;
    if (forceRefresh) {
        _dataStore.clear();
        _isPopulated = false;
    }

    logger::info("Iniciando escaneamento de FormTypes...");

    PopulateList<RE::BGSPerk>("Perk", IsValidPerk);

    PopulateList<RE::TESGlobal>("Global", [](RE::TESGlobal* global) -> bool {
        return global != nullptr;
    });

    PopulateList<RE::TESObjectBOOK>("Book", [](RE::TESObjectBOOK* book) -> bool {
        return book != nullptr;
    });

    _isPopulated = true;
    for (auto cb : _readyCallbacks) {
        if (cb) cb();
    }
    _readyCallbacks.clear();
}

void Manager::RefreshLists(std::string_view a_signatures) {
    const auto includes = [a_signatures](std::string_view a_signature) {
        std::size_t begin = 0;
        while (begin <= a_signatures.size()) {
            const auto end = a_signatures.find(',', begin);
            auto token = a_signatures.substr(begin, end == std::string_view::npos ? a_signatures.size() - begin : end - begin);
            while (!token.empty() && token.front() == ' ') token.remove_prefix(1);
            while (!token.empty() && token.back() == ' ') token.remove_suffix(1);
            if (token == a_signature) return true;
            if (end == std::string_view::npos) break;
            begin = end + 1;
        }
        return false;
    };

    if (a_signatures.empty() || includes("All")) {
        PopulateAllLists(true);
        return;
    }
    if (includes("PERK")) PopulateList<RE::BGSPerk>("Perk", IsValidPerk);
    if (includes("GLOB")) {
        PopulateList<RE::TESGlobal>("Global", [](RE::TESGlobal* global) { return global != nullptr; });
    }
    if (includes("BOOK")) {
        PopulateList<RE::TESObjectBOOK>("Book", [](RE::TESObjectBOOK* book) { return book != nullptr; });
    }
}

const std::vector<InternalFormInfo>& Manager::GetList(const std::string& typeName) {
    static std::vector<InternalFormInfo> empty;
    auto it = _dataStore.find(typeName);
    if (it != _dataStore.end()) {
        return it->second;
    }
    return empty;
}

void Manager::RegisterReadyCallback(std::function<void()> callback) {
    if (_isPopulated) {
        callback();
    }
    else {
        _readyCallbacks.push_back(callback);
    }
}

const InternalFormInfo* Manager::GetInfoByID(const std::string& type, RE::FormID id) {
    const auto& list = GetList(type);
    for (const auto& info : list) {
        if (info.formID == id) {
            return &info;
        }
    }
    return nullptr;
}

void Manager::Save(SKSE::SerializationInterface*) {}

void Manager::Load(SKSE::SerializationInterface*) {}

void Manager::Revert(SKSE::SerializationInterface*) {
    _isPopulated = false;
    _dataStore.clear();
    _readyCallbacks.clear();
}

std::string Manager::ToUTF8(std::string_view a_str) {
    if (a_str.empty()) return "";

    std::string srcString(a_str);

    if (IsValidUTF8(srcString)) {
        return srcString;
    }

    int wlen = MultiByteToWideChar(CP_ACP, 0, srcString.c_str(), -1, nullptr, 0);
    if (wlen <= 0) return srcString;

    std::wstring wstr(wlen, 0);
    MultiByteToWideChar(CP_ACP, 0, srcString.c_str(), -1, &wstr[0], wlen);

    int u8len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (u8len <= 0) return srcString;

    std::string u8str(u8len, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &u8str[0], u8len, nullptr, nullptr);

    if (!u8str.empty() && u8str.back() == '\0') u8str.pop_back();

    return u8str;
}

template <typename T>
void Manager::PopulateList(const std::string& a_typeName, std::function<bool(T*)> a_filter) {
    auto dataHandler = RE::TESDataHandler::GetSingleton();
    if (!dataHandler) return;

    auto& list = _dataStore[a_typeName];
    list.clear();

    const auto& forms = dataHandler->GetFormArray<T>();
    list.reserve(forms.size());

    for (const auto& form : forms) {
        if (!form) continue;

        if (form->IsDeleted() || form->IsIgnored()) {
            continue;
        }

        if (a_filter && !a_filter(form)) {
            continue;
        }

        RE::FormID currentID = 0;
        std::string currentPlugin = "Unknown";

        try {
            currentID = form->GetFormID();

            if (auto file = form->GetFile(0)) {
                currentPlugin = std::string(file->GetFilename());
            }
            else {
                currentPlugin = "Dynamic";
            }

            InternalFormInfo info;
            info.formID = currentID;
            info.formType = a_typeName;
            info.pluginName = ToUTF8(currentPlugin);

            std::string rawEditorID = clib_util::editorID::get_editorID(form);
            info.editorID = ToUTF8(rawEditorID);

            std::string rawName = "";
            if (form->Is(RE::FormType::NPC)) {
                if (auto npc = form->As<RE::TESNPC>()) {
                    rawName = npc->fullName.c_str();
                }
            }
            else if (auto fullName = form->As<RE::TESFullName>()) {
                rawName = fullName->fullName.c_str();
            }

            info.name = ToUTF8(rawName);
            info.description = "";
            info.nextPerkId = "";

            if (auto perk = form->As<RE::BGSPerk>()) {
                RE::BSString descStr;
                perk->TESDescription::GetDescription(descStr, perk);
                info.description = ToUTF8(descStr.c_str());

                if (perk->nextPerk) {
                    auto npFile = perk->nextPerk->GetFile(0);
                    std::string npPlugin = npFile ? std::string(npFile->GetFilename()) : "Dynamic";
                    uint32_t npLocalID = (perk->nextPerk->GetFormID() & 0xFF000000) == 0xFE000000 ?
                        (perk->nextPerk->GetFormID() & 0xFFF) :
                        (perk->nextPerk->GetFormID() & 0xFFFFFF);
                    info.nextPerkId = fmt::format("{}|{:X}", npPlugin, npLocalID);
                }
            }

            list.push_back(info);
        }
        catch (const std::exception& e) {
            logger::error("[PopulateList] Critical error on item {:08X} of plugin '{}' (Type: {}). Error: {}",
                currentID, currentPlugin, a_typeName, e.what());
        }
        catch (...) {
            logger::error("[PopulateList] Uknown error on item {:08X} of plugin '{}' (Type: {})",
                currentID, currentPlugin, a_typeName);
        }
    }
    logger::info("Carregados {} itens do tipo {}", list.size(), a_typeName);
}
