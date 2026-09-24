#pragma once

#include <cstdint>

namespace MoreRagdollAPI {
    inline constexpr std::uint32_t API_VERSION = 1;

    class Interface {
    public:
        virtual ~Interface() = default;
        virtual std::uint32_t GetVersion() const noexcept = 0;

        virtual bool Enable(std::uint32_t actorFormID, float safetyTimeoutSeconds) = 0;
        virtual bool Disable(std::uint32_t actorFormID) = 0;
        virtual bool Hold(std::uint32_t actorFormID, float seconds) = 0;
        virtual bool IsHeld(std::uint32_t actorFormID) const = 0;
        virtual bool Adopt(std::uint32_t actorFormID, float safetyTimeoutSeconds) = 0;
        virtual bool StartRagdoll(
            std::uint32_t actorFormID,
            float durationSeconds,
            bool forceGetUpOnTimeout) = 0;
    };

    using GetInterface_t = Interface* (*)();
}
