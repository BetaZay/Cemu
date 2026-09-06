add_library(CemuCommon
	BaristaAppHook.cpp
	BaristaAppHook.h
	BaristaAppHookColorTest.h
  betype.h
  cpu_features.cpp
  cpu_features.h
  enumFlags.h
  ExceptionHandler/ExceptionHandler.cpp
  ExceptionHandler/ExceptionHandler.h
  FileStream.h
  GLInclude/glext.h
  GLInclude/glFunctions.h
  GLInclude/GLInclude.h
  GLInclude/glxext.h
  GLInclude/khrplatform.h
  GLInclude/wglext.h
  MemPtr.h
  platform.h
  precompiled.cpp
  precompiled.h
  socket.h
  StackAllocator.h
  SysAllocator.cpp
  SysAllocator.h
  CafeString.h
  version.h
)

add_library(CemuBaristaAppHookColorPattern STATIC BaristaAppHookColorTest.cpp)
target_include_directories(CemuBaristaAppHookColorPattern PUBLIC "../")
target_compile_features(CemuBaristaAppHookColorPattern PUBLIC cxx_std_20)
set_property(TARGET CemuBaristaAppHookColorPattern PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
# Do not let a Debug build's pixel-generation cost confound the streaming test.
target_compile_options(CemuBaristaAppHookColorPattern PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang>:-O2>)
target_link_libraries(CemuCommon PRIVATE CemuBaristaAppHookColorPattern)
add_executable(CemuBaristaAppHookColorTestTests EXCLUDE_FROM_ALL BaristaAppHookColorTestTests.cpp)
target_link_libraries(CemuBaristaAppHookColorTestTests PRIVATE CemuBaristaAppHookColorPattern)
set_property(TARGET CemuBaristaAppHookColorTestTests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set_property(TARGET CemuCommon PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(WIN32)
	target_sources(CemuCommon PRIVATE
		windows/platform.cpp
		windows/platform.h
		windows/FileStream_win32.cpp
		ExceptionHandler/ExceptionHandler_win32.cpp
	)
else()
target_sources(CemuCommon
PRIVATE
    unix/date.h
    unix/fast_float.h
    unix/platform.cpp
    unix/platform.h
    unix/FileStream_unix.cpp
    unix/FileStream_unix.h
    ExceptionHandler/ExceptionHandler_posix.cpp
)
endif()

if(LINUX)
	target_sources(CemuCommon PRIVATE
		ExceptionHandler/ELFSymbolTable.cpp
		ExceptionHandler/ELFSymbolTable.h
	)
endif()

# CemuCommon always builts the precompiled header
target_precompile_headers(CemuCommon PRIVATE precompiled.h)
target_include_directories(CemuCommon PUBLIC "../")
if(TARGET drc_ipc)
	target_link_libraries(CemuCommon PRIVATE drc_ipc)
endif()

target_link_libraries(CemuCommon PRIVATE
	glm::glm
)

if (UNIX AND NOT APPLE)
	target_link_libraries(CemuCommon PRIVATE X11::X11 X11::Xrender X11::Xutil)
endif()

# PUBLIC because:
# - boost/nowide/convert.hpp is included in precompiled.h
# - boost/predef/os.h is included in platform.h
# - fmt/core.h is included in precompiled.h
target_link_libraries(CemuCommon PUBLIC
	Threads::Threads # necessary because some targets may need -pthreads
	Boost::nowide
	Boost::headers
	fmt::fmt
)
