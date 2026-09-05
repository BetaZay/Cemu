#include "DrcdOverlay.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace DrcdOverlay
{
namespace
{
float Edge(ImVec2 a, ImVec2 b, ImVec2 p)
{
    return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
}
bool Inclusive(ImVec2 a, ImVec2 b)
{
    return b.y > a.y || (b.y == a.y && b.x < a.x);
}
}

std::vector<uint8_t> Rasterize(const ImDrawData& data, ImTextureID fontTexture,
    const unsigned char* alpha, int atlasWidth, int atlasHeight, unsigned width, unsigned height)
{
    if (!data.Valid || !alpha || atlasWidth <= 0 || atlasHeight <= 0 ||
        data.DisplaySize.x <= 0 || data.DisplaySize.y <= 0 || !width || !height || width > 4096 || height > 4096)
        return {};
    std::vector<uint8_t> rgb(size_t(width) * height * 3, 24);
    const float sx = width / data.DisplaySize.x, sy = height / data.DisplaySize.y;
    auto position = [&](ImVec2 p) { return ImVec2((p.x - data.DisplayPos.x) * sx, (p.y - data.DisplayPos.y) * sy); };
    auto sample = [&](ImVec2 uv) {
        const float x = std::clamp(uv.x * atlasWidth - 0.5f, 0.0f, float(atlasWidth - 1));
        const float y = std::clamp(uv.y * atlasHeight - 0.5f, 0.0f, float(atlasHeight - 1));
        const int x0 = int(x), y0 = int(y), x1 = std::min(x0 + 1, atlasWidth - 1), y1 = std::min(y0 + 1, atlasHeight - 1);
        const float fx = x - x0, fy = y - y0;
        return ((alpha[y0 * atlasWidth + x0] * (1 - fx) + alpha[y0 * atlasWidth + x1] * fx) * (1 - fy) +
            (alpha[y1 * atlasWidth + x0] * (1 - fx) + alpha[y1 * atlasWidth + x1] * fx) * fy) / 255.0f;
    };
    for (int listIndex = 0; listIndex < data.CmdListsCount; ++listIndex)
    {
        const auto& list = *data.CmdLists[listIndex];
        for (const auto& command : list.CmdBuffer)
        {
            // Never execute renderer callbacks or sample unrelated GPU textures.
            if (command.UserCallback || command.TextureId != fontTexture) continue;
            const ImVec2 clipMin = position({command.ClipRect.x, command.ClipRect.y});
            const ImVec2 clipMax = position({command.ClipRect.z, command.ClipRect.w});
            for (unsigned element = 0; element + 2 < command.ElemCount; element += 3)
            {
                const size_t index = size_t(command.IdxOffset) + element;
                if (index + 2 >= size_t(list.IdxBuffer.Size)) break;
                std::array<ImDrawVert, 3> v;
                bool valid = true;
                for (unsigned i = 0; i < 3; ++i)
                {
                    const size_t vertex = size_t(command.VtxOffset) + list.IdxBuffer[index + i];
                    if (vertex >= size_t(list.VtxBuffer.Size)) { valid = false; break; }
                    v[i] = list.VtxBuffer[vertex]; v[i].pos = position(v[i].pos);
                }
                if (!valid) continue;
                float area = Edge(v[0].pos, v[1].pos, v[2].pos);
                if (!std::isfinite(area) || std::abs(area) < 0.00001f) continue;
                if (area < 0) { std::swap(v[1], v[2]); area = -area; }
                constexpr unsigned shifts[]{IM_COL32_R_SHIFT, IM_COL32_G_SHIFT, IM_COL32_B_SHIFT, IM_COL32_A_SHIFT};
                const bool solid = v[0].col == v[1].col && v[0].col == v[2].col &&
                    v[0].uv.x == v[1].uv.x && v[0].uv.x == v[2].uv.x &&
                    v[0].uv.y == v[1].uv.y && v[0].uv.y == v[2].uv.y;
                std::array<float,4> solidColor{};
                for (unsigned c=0;c<4;++c) solidColor[c] = (v[0].col >> shifts[c]) & 255;
                const float solidOpacity = solid ? sample(v[0].uv) * solidColor[3] / 255.0f : 0;
                const int left = int(std::floor(std::clamp(std::max(clipMin.x, std::min({v[0].pos.x,v[1].pos.x,v[2].pos.x})), 0.0f, float(width))));
                const int top = int(std::floor(std::clamp(std::max(clipMin.y, std::min({v[0].pos.y,v[1].pos.y,v[2].pos.y})), 0.0f, float(height))));
                const int right = int(std::ceil(std::clamp(std::min(clipMax.x, std::max({v[0].pos.x,v[1].pos.x,v[2].pos.x})), 0.0f, float(width))));
                const int bottom = int(std::ceil(std::clamp(std::min(clipMax.y, std::max({v[0].pos.y,v[1].pos.y,v[2].pos.y})), 0.0f, float(height))));
                for (int y = top; y < bottom; ++y)
                    for (int x = left; x < right; ++x)
                    {
                        ImVec2 p(x + 0.5f, y + 0.5f);
                        if (p.x < clipMin.x || p.y < clipMin.y || p.x >= clipMax.x || p.y >= clipMax.y) continue;
                        const std::array<float, 3> e{Edge(v[1].pos,v[2].pos,p), Edge(v[2].pos,v[0].pos,p), Edge(v[0].pos,v[1].pos,p)};
                        bool inside = true;
                        for (unsigned i = 0; i < 3; ++i)
                            if (e[i] < 0 || (e[i] == 0 && !Inclusive(v[(i+1)%3].pos,v[(i+2)%3].pos))) inside = false;
                        if (!inside) continue;
                        if (solid)
                        {
                            for (unsigned c=0;c<3;++c)
                            {
                                auto& dst = rgb[(size_t(y)*width+x)*3+c];
                                dst = static_cast<uint8_t>(std::clamp(std::lround(solidColor[c]*solidOpacity + dst*(1-solidOpacity)),0L,255L));
                            }
                            continue;
                        }
                        std::array<float, 4> color{};
                        ImVec2 uv{};
                        for (unsigned i = 0; i < 3; ++i)
                        {
                            const float weight = e[i] / area;
                            uv.x += v[i].uv.x * weight; uv.y += v[i].uv.y * weight;
                            for (unsigned c = 0; c < 4; ++c) color[c] += ((v[i].col >> shifts[c]) & 255) * weight;
                        }
                        const float opacity = std::clamp(sample(uv) * color[3] / 255.0f, 0.0f, 1.0f);
                        for (unsigned c = 0; c < 3; ++c)
                        {
                            auto& dst = rgb[(size_t(y) * width + x) * 3 + c];
                            dst = static_cast<uint8_t>(std::clamp(std::lround(color[c] * opacity + dst * (1 - opacity)), 0L, 255L));
                        }
                    }
            }
        }
    }
    return rgb;
}
}
