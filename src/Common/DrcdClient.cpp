#include "Common/DrcdClient.h"
#include "Common/DrcdColorTest.h"
#include "Cemu/Logging/CemuLogging.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <thread>

#if BOOST_OS_LINUX
#include "drc_ipc/media_bridge.h"
namespace
{
std::atomic<std::shared_ptr<drc_ipc::MediaBridge>> s_bridge;
std::atomic_bool s_gameActive{false};
std::atomic_bool s_colorTest{false};
std::jthread s_colorWorker;
}
#endif

namespace DrcdClient
{
void Initialize(std::vector<uint8> idleRgb, unsigned width, unsigned height)
{
#if BOOST_OS_LINUX
	const char* path = std::getenv("CEMU_DRCD_SOCKET");
	if (!path || !*path) return;
	auto bridge = std::make_shared<drc_ipc::MediaBridge>(false);
	bridge->submit_rgb(std::move(idleRgb), width, height, true);
	std::string error;
	if (!bridge->start(path, error))
	{
		cemuLog_log(LogType::Force, "drcd client could not start: {}", error);
		return;
	}
	bridge->set_active(s_gameActive);
	s_bridge.store(bridge);
	cemuLog_log(LogType::Force, "drcd client enabled: {} (pairing and Wi-Fi remain in drcd)", path);
	const char* colorTest = std::getenv("CEMU_DRCD_COLOR_TEST");
	if (colorTest && std::string_view(colorTest) == "1")
	{
		s_colorTest = true;
		bridge->set_active(true);
		cemuLog_log(LogType::Force, "drcd color test enabled: 10s black lead-in after IPC connection, 32s loop; no game needed");
		s_colorWorker = std::jthread([bridge](std::stop_token stop) {
			using Clock = std::chrono::steady_clock;
			auto origin = Clock::time_point{};
			auto deadline = Clock::now();
			uint64 previousPhase = UINT64_MAX;
			while (!stop.stop_requested())
			{
				const auto now = Clock::now();
				if (!bridge->connected())
				{
					origin = {};
					previousPhase = UINT64_MAX;
				}
				else
				{
					if (origin == Clock::time_point{})
					{
						origin = now;
						cemuLog_log(LogType::Force, "drcd color test: connected; black lead-in begins");
					}
					const auto connectedUs = std::chrono::duration_cast<std::chrono::microseconds>(now - origin).count();
					const uint64 elapsedUs = connectedUs > 10000000 ? connectedUs - 10000000 : 0;
					const uint64 phase = elapsedUs / DrcdColorTest::PhaseUs;
					if (connectedUs >= 10000000 && phase != previousPhase)
					{
						previousPhase = phase;
						const auto unixUs = std::chrono::duration_cast<std::chrono::microseconds>(
							std::chrono::system_clock::now().time_since_epoch()).count();
						cemuLog_log(LogType::Force, "drcd color test: cycle={} phase={} source_us={} unix_us={} {}",
							phase / DrcdColorTest::Phases.size(), phase % DrcdColorTest::Phases.size(),
							elapsedUs, unixUs, DrcdColorTest::Phases[phase % DrcdColorTest::Phases.size()]);
					}
					bridge->submit_rgb(DrcdColorTest::Render(elapsedUs), DrcdColorTest::Width, DrcdColorTest::Height);
				}
				// Bound catch-up: never queue a burst of overdue diagnostic frames.
				deadline = std::max(deadline + std::chrono::microseconds(16683), Clock::now());
				std::this_thread::sleep_until(deadline);
			}
		});
	}
#endif
}
void Shutdown()
{
#if BOOST_OS_LINUX
	s_colorWorker.request_stop();
	if (s_colorWorker.joinable()) s_colorWorker.join();
	s_colorTest = false;
	s_gameActive = false;
	if (auto bridge = s_bridge.exchange({})) bridge->stop();
#endif
}
void SetGameActive(bool active)
{
#if BOOST_OS_LINUX
	s_gameActive = active;
	if (auto bridge = s_bridge.load()) bridge->set_active(active || s_colorTest);
#endif
}
bool WantsFrame()
{
#if BOOST_OS_LINUX
	auto bridge = s_bridge.load();
	if (s_colorTest || !s_gameActive || !bridge || !bridge->connected()) return false;
	// Only the GPU thread calls this. Avoid duplicate capture on repeated scanout.
	static auto lastCapture = std::chrono::steady_clock::time_point{};
	const auto now = std::chrono::steady_clock::now();
	if (now - lastCapture < std::chrono::milliseconds(14)) return false;
	lastCapture = now;
	return true;
#else
	return false;
#endif
}
void SubmitFrame(std::vector<uint8> rgb, unsigned width, unsigned height)
{
#if BOOST_OS_LINUX
	if (auto bridge = s_bridge.load(); bridge && s_gameActive && !s_colorTest)
		bridge->submit_rgb(std::move(rgb), width, height);
#endif
}
void SubmitAudio(std::span<const sint16> samples, unsigned channels)
{
#if BOOST_OS_LINUX
	auto bridge = s_bridge.load();
	if (s_colorTest || !bridge || !s_gameActive || !bridge->connected() || channels < 1 || channels > 6 || samples.size() % channels) return;
	std::vector<sint16> stereo;
	stereo.reserve(samples.size() / channels * 2);
	for (size_t i = 0; i < samples.size(); i += channels)
	{
		stereo.push_back(samples[i]);
		stereo.push_back(samples[i + (channels > 1 ? 1 : 0)]);
	}
	bridge->submit_pcm(stereo);
#endif
}
bool ReadInput(std::array<uint8, 128>& report)
{
#if BOOST_OS_LINUX
	if (auto bridge = s_bridge.load()) return bridge->read_input(report);
#endif
	return false;
}
}
