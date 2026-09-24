#pragma once

#include <string>
#include <vector>
#include <map>
#include <functional>
#include "ClibUtil/editorID.hpp"
#include <mutex>
#include <unordered_set>

namespace FormUtil {
    const RE::TESFile* GetMasterFile(RE::TESForm* ref);
    std::string NormalizeFormID(RE::TESForm* form);
    RE::FormID FormIDFromString(const std::string& str);
}

struct InternalFormInfo {
    RE::FormID formID;
    std::string editorID;
    std::string name;
    std::string pluginName;
    std::string formType;
    std::string description;
    std::string nextPerkId;

    // Helper for UI
    std::string GetDisplayName() const {
        if (!name.empty()) return name;
        if (!editorID.empty()) return editorID;
        return std::to_string(formID);
    }
};



class Manager {
public:
    static Manager* GetSingleton() {
        static Manager singleton;
        return &singleton;
    }

    void PopulateAllLists(bool forceRefresh = false);
    void RefreshLists(std::string_view a_signatures);

    static std::string ToUTF8(std::string_view a_str);
    const std::vector<InternalFormInfo>& GetList(const std::string& typeName);
    void RegisterReadyCallback(std::function<void()> callback);


    // --- NOVOS MÉTODOS DE SAVE/LOAD (SKSE) ---
    void Save(SKSE::SerializationInterface* a_intfc);
    void Load(SKSE::SerializationInterface* a_intfc);
    void Revert(SKSE::SerializationInterface* a_intfc);


    const InternalFormInfo* GetInfoByID(const std::string& type, RE::FormID id);

    bool _isPopulated = false;

private:
    Manager() = default;

    template <typename T>
    void PopulateList(const std::string& a_typeName, std::function<bool(T*)> a_filter = nullptr);

    std::map<std::string, std::vector<InternalFormInfo>> _dataStore;
    std::vector<std::function<void()>> _readyCallbacks;
};
