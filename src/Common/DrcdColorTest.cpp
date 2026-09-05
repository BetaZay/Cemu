#include "DrcdColorTest.h"

namespace DrcdColorTest
{
std::vector<uint8> Render(uint64 elapsedUs)
{
	const auto phase = (elapsedUs / PhaseUs) % Phases.size();
	const uint32 progress = static_cast<uint32>((elapsedUs % PhaseUs) * 255 / (PhaseUs - 1));
	const bool detail = phase == 10 || phase == 12 || phase >= 14;
	const bool fadeIn = phase == 6 || phase == 8 || phase == 14;
	const bool fadeOut = phase == 7 || phase == 9 || phase == 15;
	const uint32 gain = fadeIn ? progress : fadeOut ? 255 - progress : 255;
	std::array<uint8, 3> base{};
	if (phase == 1 || phase == 6 || phase == 7 || phase == 11) base = {255, 0, 0};
	if (phase == 2) base = {0, 255, 0};
	if (phase == 3) base = {0, 0, 255};
	if (phase == 4 || phase == 8 || phase == 9) base = {255, 255, 255};
	std::vector<uint8> rgb(Width * Height * 3);
	for (uint32 y = 0; y < Height; ++y)
		for (uint32 x = 0; x < Width; ++x)
		{
			auto color = base;
			if (detail)
			{
				const uint8 level = ((x / 16 + y / 16) & 1) ? 224 : 32;
				color = {level, static_cast<uint8>((x * 255) / (Width - 1)),
					static_cast<uint8>((y * 255) / (Height - 1))};
			}
			for (size_t channel = 0; channel < 3; ++channel)
				rgb[(y * Width + x) * 3 + channel] = static_cast<uint8>(color[channel] * gain / 255);
		}
	return rgb;
}
}
