#pragma once
#include <cstdint>
#include <vector>
#include <imgui.h>

namespace DrcdOverlay
{
// Font-atlas UI only: no game textures, GPU readback or guest memory access.
std::vector<uint8_t> Rasterize(const ImDrawData& data, ImTextureID fontTexture,
    const unsigned char* alpha, int atlasWidth, int atlasHeight,
    unsigned width = 864, unsigned height = 480);
// Called after ImGui::Render on the GPU thread. Implemented in Renderer.cpp.
void PublishKeyboard(bool mainWindow);
}
