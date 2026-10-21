#pragma once
#include <cstdint>
namespace SDKInGame
{
    // Private OGF extension, used only by the live SDK scene exporter.
    constexpr uint32_t previewLightingChunk = 0x3f53444b;
    constexpr uint32_t previewLightingVersion = 1;
    struct PreviewLighting { float hemi, sun; };
}
