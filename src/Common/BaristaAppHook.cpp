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
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
namespace {
using Clock=std::chrono::steady_clock;
constexpr size_t chunk=16384, max_audio=9600, frame_bytes=864*480*3/2;
enum Type:uint32 { Video=1,Idle=2,Active=3,Pcm=4,Input=5 };
void put(uint8* p,uint32 v){for(unsigned i=0;i<4;i++)p[i]=v>>(8*i);}
uint32 get(const uint8* p){return uint32(p[0])|uint32(p[1])<<8|uint32(p[2])<<16|uint32(p[3])<<24;}
struct Rgb { std::vector<uint8> b; unsigned w=0,h=0; };
std::vector<uint8> i420(const Rgb& f) {
 if(!f.w||!f.h||f.b.size()!=size_t(f.w)*f.h*3)return {};
 std::vector<uint8> o(frame_bytes,128); for(size_t y=0;y<480;y+=2)for(size_t x=0;x<864;x+=2){int rs=0,gs=0,bs=0;for(size_t dy=0;dy<2;dy++)for(size_t dx=0;dx<2;dx++){auto s=(((y+dy)*f.h/480)*f.w+(x+dx)*f.w/864)*3;int r=f.b[s],g=f.b[s+1],b=f.b[s+2];o[(y+dy)*864+x+dx]=std::clamp(((66*r+129*g+25*b+128)>>8)+16,16,235);rs+=r;gs+=g;bs+=b;}int r=rs/4,g=gs/4,b=bs/4;auto c=(y/2)*432+x/2;o[864*480+c]=std::clamp(((-38*r-74*g+112*b+128)>>8)+128,16,240);o[864*480*5/4+c]=std::clamp(((112*r-94*g-18*b+128)>>8)+128,16,240);}return o;
}
class Client { public:
 ~Client(){stop();} bool start(std::string p,std::string& e){if(p.empty()||p.size()>=sizeof(sockaddr_un::sun_path)){e="invalid Barista socket path";return false;}path=std::move(p);worker=std::jthread([this]{run();});return true;} void stop(){stopping=true;if(worker.joinable())worker.join();} bool connected()const{return linked;}
 void active(bool a){std::lock_guard l(m);on=a;if(!a){audio.clear();pending={};}} void rgb(std::vector<uint8> b,unsigned w,unsigned h,bool idle=false){if(!w||!h||b.size()!=size_t(w)*h*3)return;std::unique_lock l(m,std::try_to_lock);if(!l)return;if(idle){logo={std::move(b),w,h};logo_rev++;}else pending={std::move(b),w,h};} void pcm(std::span<const sint16> s){std::unique_lock l(m,std::try_to_lock);if(!l||!on||!linked)return;audio.insert(audio.end(),s.begin(),s.end());while(audio.size()>max_audio)audio.pop_front();} bool input(std::array<uint8,128>& r)const{std::lock_guard l(m);if(!linked||Clock::now()-input_time>std::chrono::milliseconds(500))return false;r=in;return true;}
 private:
 bool sendp(int fd,Type t,uint32 id,uint32 off,std::span<const uint8> d){if(d.size()>chunk)return false;std::array<uint8,16+chunk> p{};put(p.data(),0x3147554d);put(p.data()+4,t);put(p.data()+8,id);put(p.data()+12,off);std::copy(d.begin(),d.end(),p.begin()+16);return send(fd,p.data(),d.size()+16,MSG_DONTWAIT|MSG_NOSIGNAL)==ssize_t(d.size()+16);} bool sendf(int fd,Type t,uint32 id,const std::vector<uint8>& f){for(size_t o=0;o<f.size();o+=chunk)if(!sendp(fd,t,id,o,std::span(f).subspan(o,std::min(chunk,f.size()-o))))return false;return true;}
 void session(int fd){linked=true;uint32 id=0;uint64 sent_logo=0;auto beat=Clock::time_point{};while(!stopping){Rgb f,l;std::vector<uint8> p;bool a;{std::lock_guard z(m);f=std::move(pending);pending={};if(sent_logo!=logo_rev){l=logo;sent_logo=logo_rev;}a=on;while(!audio.empty()&&p.size()+2<=chunk){auto v=uint16(audio.front());audio.pop_front();p.push_back(v);p.push_back(v>>8);}}if(Clock::now()>=beat){std::array<uint8,1> q{uint8(a)};if(!sendp(fd,Active,0,0,q))break;beat=Clock::now()+std::chrono::milliseconds(100);}if((!l.b.empty()&&!sendf(fd,Idle,++id,i420(l)))||(!f.b.empty()&&!sendf(fd,Video,++id,i420(f)))||(!p.empty()&&!sendp(fd,Pcm,0,0,p)))break;pollfd q{fd,POLLIN,0};poll(&q,1,2);if(q.revents&(POLLERR|POLLHUP|POLLNVAL))break;std::array<uint8,16+chunk> b;auto n=recv(fd,b.data(),b.size(),MSG_DONTWAIT|MSG_TRUNC);if(n>=0){if(n!=144||get(b.data())!=0x3147554d||get(b.data()+4)!=Input)break;std::lock_guard z(m);std::copy_n(b.data()+16,128,in.begin());input_time=Clock::now();}}linked=false;close(fd);}
 void run(){while(!stopping){int fd=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC|SOCK_NONBLOCK,0);sockaddr_un a{};a.sun_family=AF_UNIX;std::memcpy(a.sun_path,path.c_str(),path.size()+1);if(fd>=0&&connect(fd,reinterpret_cast<sockaddr*>(&a),sizeof(a))==0){session(fd);continue;}if(fd>=0)close(fd);std::this_thread::sleep_for(std::chrono::milliseconds(100));}}
 std::atomic_bool stopping=false,linked=false;std::jthread worker;std::string path;mutable std::mutex m;Rgb pending,logo;std::deque<sint16> audio;std::array<uint8,128> in{};Clock::time_point input_time{};bool on=false;uint64 logo_rev=0;
};
std::atomic<std::shared_ptr<Client>> bridge; std::atomic_bool game=false;
std::string path(){if(auto p=std::getenv("BARISTA_MUG_SOCKET");p&&*p)return p;return "/run/barista/media-"+std::to_string(static_cast<unsigned long>(geteuid()))+".sock";}
}
#endif
namespace BaristaAppHook {
void Initialize(std::vector<uint8> rgb,unsigned w,unsigned h)
{
#if BOOST_OS_LINUX
	auto b=std::make_shared<Client>();b->rgb(std::move(rgb),w,h,true);std::string e;if(!b->start(path(),e)){cemuLog_log(LogType::Force,"Barista AppHook client could not start: {}",e);return;}b->active(game);bridge.store(b);cemuLog_log(LogType::Force,"Barista AppHook client enabled: {}",path());
#endif
}
void Shutdown()
{
#if BOOST_OS_LINUX
	game=false;if(auto b=bridge.exchange({}))b->stop();
#endif
}
void SetGameActive(bool a)
{
#if BOOST_OS_LINUX
	game=a;if(auto b=bridge.load())b->active(a);
#endif
}
bool WantsFrame()
{
#if BOOST_OS_LINUX
	auto b=bridge.load();if(!game||!b||!b->connected())return false;static auto last=Clock::time_point{};auto now=Clock::now();if(now-last<std::chrono::milliseconds(14))return false;last=now;return true;
#else
	return false;
#endif
}
void SubmitFrame(std::vector<uint8> r,unsigned w,unsigned h)
{
#if BOOST_OS_LINUX
	if(auto b=bridge.load();b&&game)b->rgb(std::move(r),w,h);
#endif
}
void SubmitAudio(std::span<const sint16> s,unsigned c)
{
#if BOOST_OS_LINUX
	auto b=bridge.load();if(!b||!game||!b->connected()||c<1||c>6||s.size()%c)return;std::vector<sint16> o;o.reserve(s.size()/c*2);for(size_t i=0;i<s.size();i+=c){o.push_back(s[i]);o.push_back(s[i+(c>1)]);}b->pcm(o);
#endif
}
bool ReadInput(std::array<uint8,128>& r)
{
#if BOOST_OS_LINUX
	if(auto b=bridge.load())return b->input(r);
#endif
	return false;
}
}
