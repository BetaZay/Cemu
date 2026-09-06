add_library(DrcdOverlayRaster STATIC DrcdOverlay.cpp)
target_include_directories(DrcdOverlayRaster PUBLIC "../../dependencies/imgui/")
target_compile_features(DrcdOverlayRaster PUBLIC cxx_std_20)
# Cemu links the static CRT on Windows; keep this helper library compatible.
set_property(TARGET DrcdOverlayRaster PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
# This pixel loop must remain realtime even in the emulator's Debug build.
target_compile_options(DrcdOverlayRaster PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang>:-O2>)

add_executable(CemuDrcdOverlayTests EXCLUDE_FROM_ALL
	DrcdOverlayTests.cpp
	../../dependencies/imgui/imgui.cpp
	../../dependencies/imgui/imgui_draw.cpp
	../../dependencies/imgui/imgui_tables.cpp
	../../dependencies/imgui/imgui_widgets.cpp
)
target_link_libraries(CemuDrcdOverlayTests PRIVATE DrcdOverlayRaster)
set_property(TARGET CemuDrcdOverlayTests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_library(imguiImpl
	imgui_extension.cpp
	imgui_extension.h
)

if (ENABLE_OPENGL)
	target_sources(imguiImpl PRIVATE
		imgui_impl_opengl3.cpp
		imgui_impl_opengl3.h
	)
endif()

if (ENABLE_VULKAN)
	target_sources(imguiImpl PRIVATE
		imgui_impl_vulkan.cpp
		imgui_impl_vulkan.h
	)
endif()

if (ENABLE_METAL)
    target_sources(imguiImpl PRIVATE
        imgui_impl_metal.mm
        imgui_impl_metal.h
    )

    target_compile_definitions(imguiImpl PRIVATE IMGUI_IMPL_METAL_CPP)
endif ()

set_property(TARGET imguiImpl PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

cemu_use_precompiled_header(imguiImpl)

target_include_directories(imguiImpl PUBLIC "../")

# imgui source files
target_sources(imguiImpl PRIVATE
"../../dependencies/imgui/imgui.cpp"
"../../dependencies/imgui/imgui_draw.cpp"
"../../dependencies/imgui/imgui_tables.cpp"
"../../dependencies/imgui/imgui_widgets.cpp"
)

target_include_directories(imguiImpl PUBLIC "../../dependencies/imgui/")

target_link_libraries(imguiImpl PRIVATE
	DrcdOverlayRaster
	CemuCommon
	CemuGui
)
