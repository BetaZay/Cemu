#pragma once
#include <array>
#include <span>
#include <vector>
namespace DrcdClient
{
void Initialize(std::vector<uint8> idleRgb, unsigned width, unsigned height);
void Shutdown();
void SetGameActive(bool active);
bool WantsFrame();
void SubmitFrame(std::vector<uint8> rgb, unsigned width, unsigned height);
void SubmitAudio(std::span<const sint16> samples, unsigned channels);
bool ReadInput(std::array<uint8, 128>& report);
}
