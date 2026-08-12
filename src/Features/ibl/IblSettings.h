#pragma once

namespace community_shaders::ibl
{
    struct Settings
    {
        // Preserve the behavior of builds that predate the independent IBL
        // control. The runtime remains fail-closed until a validated
        // environment pair exists even when this setting is enabled.
        bool enabled{ true };
    };
}
