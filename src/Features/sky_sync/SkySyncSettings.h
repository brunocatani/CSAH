#pragma once

namespace csah::sky_sync
{
    struct Settings final
    {
        bool enabled{ true };

        [[nodiscard]] bool operator==(const Settings&) const noexcept = default;
    };
}
