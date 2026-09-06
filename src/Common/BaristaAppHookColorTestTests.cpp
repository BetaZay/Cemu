#include "Common/BaristaAppHookColorTest.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>

int main()
{
	using namespace BaristaAppHookColorTest;
	auto check = [](bool ok) { if (!ok) throw std::runtime_error("color test regression"); };
	const auto black = Render(0);
	check(black.size() == Width * Height * 3);
	check(std::all_of(black.begin(), black.end(), [](uint8 value) { return value == 0; }));
	for (uint32 phase = 1; phase <= 4; ++phase)
	{
		const auto rgb = Render(PhaseUs * phase);
		for (size_t offset = 0; offset < rgb.size(); ++offset)
			check(rgb[offset] == ((phase == 4 || offset % 3 == phase - 1) ? 255 : 0));
	}
	for (uint32 phase : {6u, 8u, 14u})
	{
		check(Render(phase * PhaseUs) == black);
		check(Render((phase + 1) * PhaseUs - 1) == Render((phase + 1) * PhaseUs));
		check(Render((phase + 2) * PhaseUs - 1) == black);
	}
	check(Render(10 * PhaseUs) == Render(12 * PhaseUs));
	check(Render(10 * PhaseUs) != black);
	check(Render(1234567) == Render(CycleUs + 1234567));
	const auto mid = Render(6 * PhaseUs + PhaseUs / 2);
	check(mid[0] == 127 && mid[1] == 0 && mid[2] == 0);
	const auto started = std::chrono::steady_clock::now();
	for (uint32 frame = 0; frame < 120; ++frame)
		check(Render(14 * PhaseUs + frame * 16683).size() == black.size());
	std::cout << "120 detailed fade frames rendered in " <<
		std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count() << "ms\n";
	std::cout << "Color cuts, fade endpoints, detail pattern, dimensions and repeatability passed\n";
}
