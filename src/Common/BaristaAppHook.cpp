#include "Common/BaristaAppHook.h"
#include "Cemu/Logging/CemuLogging.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>

#if BOOST_OS_LINUX
#include <csignal>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace {
using Clock = std::chrono::steady_clock;
constexpr size_t chunk = 16384, max_audio = 9600, frame_bytes = 864 * 480 * 3 / 2;
enum Type : uint32 { Video = 1, Idle = 2, Active = 3, Pcm = 4, Input = 5, Reject = 6 };

struct LockInfo
{
	bool locked = false;
	uint32 pid = 0;
	std::string app;
	uint64 lastSeen = 0;
};

LockInfo ReadLockFile(const std::string& lockPath)
{
	LockInfo info;
	FILE* f = fopen(lockPath.c_str(), "re");
	if (!f) return info;
	char line[256];
	while (fgets(line, sizeof(line), f))
	{
		std::string_view sv(line);
		while (!sv.empty() && (sv.back() == '\r' || sv.back() == '\n' || sv.back() == ' '))
			sv.remove_suffix(1);
		auto eq = sv.find('=');
		if (eq == std::string_view::npos) continue;
		auto key = sv.substr(0, eq);
		auto val = sv.substr(eq + 1);
		if (key == "pid")
		{
			char* end = nullptr;
			info.pid = static_cast<uint32>(strtoul(std::string(val).c_str(), &end, 10));
		}
		else if (key == "app" || key == "name")
		{
			info.app = std::string(val);
		}
		else if (key == "last_seen")
		{
			char* end = nullptr;
			info.lastSeen = strtoull(std::string(val).c_str(), &end, 10);
		}
	}
	fclose(f);
	if (info.pid > 0)
	{
		if (kill(static_cast<pid_t>(info.pid), 0) == 0 || errno == EPERM)
			info.locked = true;
	}
	return info;
}
void put(uint8* p, uint32 v) { for (unsigned i = 0; i < 4; i++) p[i] = v >> (8 * i); }
uint32 get(const uint8* p) { return uint32(p[0]) | uint32(p[1]) << 8 | uint32(p[2]) << 16 | uint32(p[3]) << 24; }
struct Rgb { std::vector<uint8> b; unsigned w = 0, h = 0; };
std::vector<uint8> i420(const Rgb& f)
{
	if (!f.w || !f.h || f.b.size() != size_t(f.w) * f.h * 3) return {};
	std::vector<uint8> o(frame_bytes, 128);
	for (size_t y = 0; y < 480; y += 2)
		for (size_t x = 0; x < 864; x += 2)
		{
			int rs = 0, gs = 0, bs = 0;
			for (size_t dy = 0; dy < 2; dy++)
				for (size_t dx = 0; dx < 2; dx++)
				{
					auto s = (((y + dy) * f.h / 480) * f.w + (x + dx) * f.w / 864) * 3;
					int r = f.b[s], g = f.b[s + 1], b = f.b[s + 2];
					o[(y + dy) * 864 + x + dx] = std::clamp(((66 * r + 129 * g + 25 * b + 128) >> 8) + 16, 16, 235);
					rs += r; gs += g; bs += b;
				}
			int r = rs / 4, g = gs / 4, b = bs / 4;
			auto c = (y / 2) * 432 + x / 2;
			o[864 * 480 + c] = std::clamp(((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128, 16, 240);
			o[864 * 480 * 5 / 4 + c] = std::clamp(((112 * r - 94 * g - 18 * b + 128) >> 8) + 128, 16, 240);
		}
	return o;
}

class Client {
public:
	~Client() { stop(); }
	bool start(std::string p, std::string& e)
	{
		if (p.empty() || p.size() >= sizeof(sockaddr_un::sun_path))
		{
			e = "invalid Barista socket path";
			return false;
		}
		path = std::move(p);
		worker = std::jthread([this] { run(); });
		return true;
	}
	void stop()
	{
		stopping = true;
		if (worker.joinable()) worker.join();
	}
	bool connected() const { return linked; }
	void active(bool a)
	{
		std::lock_guard l(m);
		on = a;
		if (!a) { audio.clear(); pending = {}; }
	}
	void rgb(std::vector<uint8> b, unsigned w, unsigned h, bool idle = false)
	{
		if (!w || !h || b.size() != size_t(w) * h * 3) return;
		if (idle)
		{
			std::lock_guard l(m);
			logo = {std::move(b), w, h};
			logo_rev++;
			return;
		}
		std::unique_lock l(m, std::try_to_lock);
		if (!l) return;
		pending = {std::move(b), w, h};
	}
	void pcm(std::span<const sint16> s)
	{
		std::unique_lock l(m, std::try_to_lock);
		if (!l || !on || !linked) return;
		audio.insert(audio.end(), s.begin(), s.end());
		while (audio.size() > max_audio) audio.pop_front();
	}
	bool input(std::array<uint8, 128>& r) const
	{
		std::lock_guard l(m);
		if (!linked || Clock::now() - input_time > std::chrono::milliseconds(500)) return false;
		r = in;
		return true;
	}

	uint64 get_frames_sent() const { return frames_sent.load(); }
	uint64 get_audio_chunks_sent() const { return audio_chunks_sent.load(); }
	uint64 get_input_reports_received() const { return input_reports_received.load(); }
	sint64 get_last_input_ms() const
	{
		std::lock_guard l(m);
		if (input_reports_received == 0) return -1;
		return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - input_time).count();
	}
	std::string get_rejection_reason() const
	{
		std::lock_guard l(m);
		return rejection_reason;
	}
	const std::string& get_path() const { return path; }

private:
	bool sendp(int fd, Type t, uint32 id, uint32 off, std::span<const uint8> d)
	{
		if (d.size() > chunk) return false;
		std::array<uint8, 16 + chunk> p{};
		put(p.data(), 0x3147554d);
		put(p.data() + 4, t);
		put(p.data() + 8, id);
		put(p.data() + 12, off);
		std::copy(d.begin(), d.end(), p.begin() + 16);
		const auto deadline = Clock::now() + std::chrono::milliseconds(100);
		do
		{
			const auto n = send(fd, p.data(), d.size() + 16, MSG_DONTWAIT | MSG_NOSIGNAL);
			if (n == ssize_t(d.size() + 16)) return true;
			if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) return false;
			pollfd q{fd, POLLOUT, 0};
			poll(&q, 1, 2);
		} while (!stopping && Clock::now() < deadline);
		return false;
	}
	bool sendf(int fd, Type t, uint32 id, const std::vector<uint8>& f)
	{
		for (size_t o = 0; o < f.size(); o += chunk)
			if (!sendp(fd, t, id, o, std::span(f).subspan(o, std::min(chunk, f.size() - o)))) return false;
		return true;
	}
	void session(int fd)
	{
		{
			std::lock_guard z(m);
			rejection_reason.clear();
		}
		linked = true;
		uint32 id = 0;
		uint64 sent_logo = 0;
		auto beat = Clock::time_point{};
		while (!stopping)
		{
			Rgb f, l;
			std::vector<uint8> p;
			bool a;
			{
				std::lock_guard z(m);
				f = std::move(pending);
				pending = {};
				if (sent_logo != logo_rev) { l = logo; sent_logo = logo_rev; }
				a = on;
				while (!audio.empty() && p.size() + 2 <= chunk)
				{
					auto v = uint16(audio.front());
					audio.pop_front();
					p.push_back(v);
					p.push_back(v >> 8);
				}
			}
			if (Clock::now() >= beat)
			{
				std::array<uint8, 1> q{uint8(a)};
				if (!sendp(fd, Active, 0, 0, q)) break;
				beat = Clock::now() + std::chrono::milliseconds(100);
			}
			if (!l.b.empty() && !sendf(fd, Idle, ++id, i420(l))) break;
			if (!f.b.empty())
			{
				if (!sendf(fd, Video, ++id, i420(f))) break;
				frames_sent++;
			}
			if (!p.empty())
			{
				if (!sendp(fd, Pcm, 0, 0, p)) break;
				audio_chunks_sent++;
			}
			pollfd q{fd, POLLIN, 0};
			poll(&q, 1, 2);
			if (q.revents & (POLLERR | POLLNVAL)) break;
			if (q.revents & (POLLIN | POLLHUP))
			{
				std::array<uint8, 16 + chunk> b;
				auto n = recv(fd, b.data(), b.size(), MSG_DONTWAIT | MSG_TRUNC);
				if (n >= 16 && get(b.data()) == 0x3147554d)
				{
					const auto type = get(b.data() + 4);
					if (type == Reject)
					{
						std::string reason(reinterpret_cast<const char*>(b.data() + 16), n - 16);
						std::lock_guard z(m);
						rejection_reason = reason;
						cemuLog_log(LogType::Force, "Barista AppHook connection rejected: {}", reason);
						break;
					}
					if (type == Input && n == 144)
					{
						std::lock_guard z(m);
						std::copy_n(b.data() + 16, 128, in.begin());
						input_time = Clock::now();
						input_reports_received++;
					}
				}
				else if (n <= 0 && (q.revents & POLLHUP))
				{
					break;
				}
			}
		}
		linked = false;
		close(fd);
	}
	void run()
	{
		while (!stopping)
		{
			const auto lock = ReadLockFile(path + ".lock");
			if (lock.locked && lock.pid != static_cast<uint32>(getpid()))
			{
				std::lock_guard l(m);
				rejection_reason = "Busy: already connected to " + (lock.app.empty() ? "another app" : lock.app) + " (PID " + std::to_string(lock.pid) + ")";
			}

			int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
			if (fd >= 0)
			{
				int buf_size = 2 * 1024 * 1024;
				setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &buf_size, sizeof(buf_size));
				setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &buf_size, sizeof(buf_size));
			}
			sockaddr_un a{};
			a.sun_family = AF_UNIX;
			std::memcpy(a.sun_path, path.c_str(), path.size() + 1);
			if (fd >= 0 && connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == 0)
			{
				{
					std::lock_guard l(m);
					if (!logo.b.empty())
					{
						auto yuv = i420(logo);
						if (yuv.size() == frame_bytes)
						{
							std::string logo_path = path + ".idle.i420";
							FILE* f = fopen(logo_path.c_str(), "wb");
							if (f)
							{
								fwrite(yuv.data(), 1, yuv.size(), f);
								fclose(f);
							}
						}
					}
				}
				session(fd);
				continue;
			}
			if (fd >= 0) close(fd);
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
	}

	std::atomic_bool stopping = false, linked = false;
	std::atomic<uint64> frames_sent = 0, audio_chunks_sent = 0, input_reports_received = 0;
	std::jthread worker;
	std::string path;
	std::string rejection_reason;
	mutable std::mutex m;
	Rgb pending, logo;
	std::deque<sint16> audio;
	std::array<uint8, 128> in{};
	Clock::time_point input_time{};
	bool on = false;
	uint64 logo_rev = 0;
};

std::atomic<std::shared_ptr<Client>> bridge;
std::atomic_bool game = false;
std::atomic_bool s_enabled = true;
std::mutex s_configMutex;
std::string s_configuredPath;
std::vector<uint8> s_idleCanvas;
unsigned s_idleWidth = 0, s_idleHeight = 0;

std::string ResolveSocketPath(const std::string& custom)
{
	if (!custom.empty()) return custom;
	if (auto p = std::getenv("BARISTA_MUG_SOCKET"); p && *p) return p;
	return "/run/barista/media-" + std::to_string(static_cast<unsigned long>(geteuid())) + ".sock";
}
}
#endif

namespace BaristaAppHook {

std::string GetDefaultSocketPath()
{
#if BOOST_OS_LINUX
	return "/run/barista/media-" + std::to_string(static_cast<unsigned long>(geteuid())) + ".sock";
#else
	return "";
#endif
}

std::string GetEffectiveSocketPath()
{
#if BOOST_OS_LINUX
	std::lock_guard lock(s_configMutex);
	return ResolveSocketPath(s_configuredPath);
#else
	return "";
#endif
}

bool IsConnected()
{
#if BOOST_OS_LINUX
	auto b = bridge.load();
	return b && b->connected();
#else
	return false;
#endif
}

Status GetStatus()
{
	Status s;
#if BOOST_OS_LINUX
	std::lock_guard lock(s_configMutex);
	s.enabled = s_enabled.load();
	s.gameActive = game.load();
	s.configuredSocketPath = s_configuredPath;
	s.effectiveSocketPath = ResolveSocketPath(s_configuredPath);

	if (!s.effectiveSocketPath.empty())
	{
		struct stat st{};
		if (stat(s.effectiveSocketPath.c_str(), &st) == 0)
			s.socketFileExists = true;

		const auto lockInfo = ReadLockFile(s.effectiveSocketPath + ".lock");
		if (lockInfo.locked && lockInfo.pid != static_cast<uint32>(getpid()))
		{
			s.lockHolder = "Locked by " + (lockInfo.app.empty() ? "another app" : lockInfo.app) + " (PID " + std::to_string(lockInfo.pid) + ")";
		}
	}

	if (auto b = bridge.load())
	{
		s.connected = b->connected();
		s.framesSent = b->get_frames_sent();
		s.audioChunksSent = b->get_audio_chunks_sent();
		s.inputReportsReceived = b->get_input_reports_received();
		s.lastInputMsAgo = b->get_last_input_ms();
		s.rejectionReason = b->get_rejection_reason();
	}
#endif
	return s;
}

void Reconnect()
{
#if BOOST_OS_LINUX
	std::lock_guard lock(s_configMutex);
	if (!s_enabled.load()) return;

	if (auto old = bridge.exchange({}))
		old->stop();

	const auto effectivePath = ResolveSocketPath(s_configuredPath);
	auto b = std::make_shared<Client>();
	if (!s_idleCanvas.empty())
		b->rgb(s_idleCanvas, s_idleWidth, s_idleHeight, true);
	std::string e;
	if (!b->start(effectivePath, e))
	{
		cemuLog_log(LogType::Force, "Barista AppHook client could not reconnect: {}", e);
		return;
	}
	b->active(game.load());
	bridge.store(b);
	cemuLog_log(LogType::Force, "Barista AppHook client reconnected: {}", effectivePath);
#endif
}

void Reconfigure(const std::string& customSocketPath, bool enabled)
{
#if BOOST_OS_LINUX
	std::lock_guard lock(s_configMutex);
	const bool pathChanged = (s_configuredPath != customSocketPath);
	const bool enabledChanged = (s_enabled.load() != enabled);

	s_configuredPath = customSocketPath;
	s_enabled.store(enabled);

	if (!enabled)
	{
		if (auto b = bridge.exchange({}))
			b->stop();
		return;
	}

	if (pathChanged || enabledChanged || !bridge.load())
	{
		if (auto old = bridge.exchange({}))
			old->stop();

		const auto effectivePath = ResolveSocketPath(s_configuredPath);
		auto b = std::make_shared<Client>();
		if (!s_idleCanvas.empty())
			b->rgb(s_idleCanvas, s_idleWidth, s_idleHeight, true);
		std::string e;
		if (!b->start(effectivePath, e))
		{
			cemuLog_log(LogType::Force, "Barista AppHook client could not start: {}", e);
			return;
		}
		b->active(game.load());
		bridge.store(b);
		cemuLog_log(LogType::Force, "Barista AppHook client reconfigured: {}", effectivePath);
	}
#endif
}

void Initialize(std::vector<uint8> rgb, unsigned w, unsigned h, const std::string& customSocketPath, bool enabled)
{
#if BOOST_OS_LINUX
	std::lock_guard lock(s_configMutex);
	s_idleCanvas = rgb;
	s_idleWidth = w;
	s_idleHeight = h;
	s_configuredPath = customSocketPath;
	s_enabled.store(enabled);

	if (!s_enabled.load()) return;

	const auto effectivePath = ResolveSocketPath(s_configuredPath);
	auto b = std::make_shared<Client>();
	b->rgb(std::move(rgb), w, h, true);
	std::string e;
	if (!b->start(effectivePath, e))
	{
		cemuLog_log(LogType::Force, "Barista AppHook client could not start: {}", e);
		return;
	}
	b->active(game.load());
	bridge.store(b);
	cemuLog_log(LogType::Force, "Barista AppHook client enabled: {}", effectivePath);
#endif
}

void Shutdown()
{
#if BOOST_OS_LINUX
	game = false;
	if (auto b = bridge.exchange({})) b->stop();
#endif
}

void SetGameActive(bool a)
{
#if BOOST_OS_LINUX
	game = a;
	if (auto b = bridge.load()) b->active(a);
#endif
}

bool WantsFrame()
{
#if BOOST_OS_LINUX
	if (!s_enabled.load()) return false;
	auto b = bridge.load();
	if (!game || !b || !b->connected()) return false;
	static auto last = Clock::time_point{};
	auto now = Clock::now();
	if (now - last < std::chrono::milliseconds(14)) return false;
	last = now;
	return true;
#else
	return false;
#endif
}

void SubmitFrame(std::vector<uint8> r, unsigned w, unsigned h)
{
#if BOOST_OS_LINUX
	if (!s_enabled.load()) return;
	if (auto b = bridge.load(); b && game) b->rgb(std::move(r), w, h);
#endif
}

void SubmitAudio(std::span<const sint16> s, unsigned c)
{
#if BOOST_OS_LINUX
	if (!s_enabled.load()) return;
	auto b = bridge.load();
	if (!b || !game || !b->connected() || c < 1 || c > 6 || s.size() % c) return;
	std::vector<sint16> o;
	o.reserve(s.size() / c * 2);
	for (size_t i = 0; i < s.size(); i += c)
	{
		o.push_back(s[i]);
		o.push_back(s[i + (c > 1)]);
	}
	b->pcm(o);
#endif
}

bool ReadInput(std::array<uint8, 128>& r)
{
#if BOOST_OS_LINUX
	if (!s_enabled.load()) return false;
	if (auto b = bridge.load()) return b->input(r);
#endif
	return false;
}

bool ReadTouch(float& x, float& y)
{
	std::array<uint8, 128> report{};
	if (!ReadInput(report)) return false;

	// The GamePad supplies ten filtered touch samples in the DRC input report.
	const int pressure = ((report[37] >> 4) & 7) |
		(((report[39] >> 4) & 7) << 3) |
		(((report[41] >> 4) & 7) << 6) |
		(((report[43] >> 4) & 7) << 9);
	int raw_x = 0;
	int raw_y = 0;
	for (int i = 0; i < 10; ++i)
	{
		const int offset = 36 + i * 4;
		raw_x += ((report[offset + 1] & 0x0f) << 8) | report[offset];
		raw_y += ((report[offset + 3] & 0x0f) << 8) | report[offset + 2];
	}
	if (pressure == 0) return false;

	raw_x /= 10;
	raw_y /= 10;
	const float gamepad_x = std::clamp(-23.1097f + raw_x * 0.2210755f, 0.0f, 854.0f);
	const float gamepad_y = std::clamp(507.639f - raw_y * 0.12772f, 0.0f, 480.0f);
	x = gamepad_x / 854.0f;
	y = gamepad_y / 480.0f;
	return true;
}

}
