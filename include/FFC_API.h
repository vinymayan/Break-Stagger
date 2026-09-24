#pragma once

#include <string_view>

namespace FFC_API {
    constexpr const char* PluginName = "ApplyImpulse";
    constexpr uint32_t kMessage_RequestAPI = 7075;
    constexpr uint32_t kMessage_ProvideAPI = 7076;
    constexpr uint32_t kMessage_RequestAPIV2 = 7077;
    constexpr uint32_t kMessage_ProvideAPIV2 = 7078;
    class IFFCInterface {
    public:
        virtual ~IFFCInterface() = default;

        // Aplica um impulso customizado de velocidade no ator
        virtual void ApplyCustomVelocityImpulse(RE::Actor* a_actor, float a_x, float a_y, float a_z, float a_time, bool a_inflictDamage) = 0;

        // Aplica uma rotação customizada no ator
        virtual void ApplyCustomRotation(RE::Actor* a_actor, float a_yawDegrees, float a_time) = 0;

        // Retorna se o ator esta temporariamente protegido contra dano de colisao/queda
        virtual bool IsCollisionDamageSuppressed(RE::Actor* a_actor) = 0;

        // Aplica um impulso customizado com controle de momentum por canal
        virtual void ApplyCustomVelocityImpulseMomentum(
            RE::Actor* a_actor,
            float a_x,
            float a_y,
            float a_z,
            float a_time,
            bool a_inflictDamage,
            bool a_allowMomentumHorizontal,
            bool a_allowMomentumVertical) = 0;
    };

    class IFFCInterfaceV2 : public IFFCInterface {
    public:
        // Actor must already be in ragdoll. X/Y follow the actor's local axes; Z is world up.
        virtual bool ApplyRagdollImpulse(RE::Actor* a_actor, float a_x, float a_y, float a_z, bool a_inflictDamage) = 0;
    };

    inline IFFCInterface* _API = nullptr;
    inline IFFCInterfaceV2* _APIV2 = nullptr;

    // ====================================================================
    // MÉTODO 1: SKSE MESSAGING (Assíncrono)
    // ====================================================================
    inline void RequestAPI() {
        auto messaging = SKSE::GetMessagingInterface();
        if (messaging) {
            messaging->Dispatch(kMessage_RequestAPI, nullptr, 0, nullptr);
        }
    }

    inline void ReceiveAPI(SKSE::MessagingInterface::Message* message) {
        if (message->type == kMessage_ProvideAPI && message->data) {
            _API = static_cast<IFFCInterface*>(message->data);
        }
    }

    inline void RequestAPIV2() {
        if (auto messaging = SKSE::GetMessagingInterface()) {
            messaging->Dispatch(kMessage_RequestAPIV2, nullptr, 0, nullptr);
        }
    }

    inline void ReceiveAPIV2(SKSE::MessagingInterface::Message* message) {
        if (message->type == kMessage_ProvideAPIV2 && message->data) {
            _APIV2 = static_cast<IFFCInterfaceV2*>(message->data);
        }
    }

    // ====================================================================
    // MÉTODO 2: DLL EXPORT DIRECT (Síncrono / Instantâneo)
    // ====================================================================
    inline IFFCInterface* RequestAPIDirect() {
        HMODULE handle = GetModuleHandleW(L"ApplyImpulse.dll");
        if (handle) {
            auto getApiFunc = (void* (*)())GetProcAddress(handle, "GetFFCAPI");
            if (getApiFunc) {
                _API = static_cast<IFFCInterface*>(getApiFunc());
                return _API;
            }
        }
        return nullptr;
    }


    inline IFFCInterfaceV2* RequestAPIV2Direct() {
        _APIV2 = nullptr;
        HMODULE handle = GetModuleHandleW(L"ApplyImpulse.dll");
        if (handle) {
            auto getApiFunc = reinterpret_cast<void* (*)()>(GetProcAddress(handle, "GetFFCAPI2"));
            if (getApiFunc) {
                _APIV2 = static_cast<IFFCInterfaceV2*>(getApiFunc());
            }
        }
        return _APIV2;
    }
}
