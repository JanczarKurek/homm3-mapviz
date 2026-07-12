set(wallpaperapp_SRCS
		StdInc.cpp
		EntryPoint.cpp
		WallpaperRendererContext.cpp
)

set(wallpaperapp_HEADERS
		StdInc.h
		WallpaperRendererContext.h
)

assign_source_group(${wallpaperapp_SRCS} ${wallpaperapp_HEADERS})

# No WIN32 flag: this is a console app and must be able to write to stdout.
add_executable(vcmiwallpaper ${wallpaperapp_SRCS} ${wallpaperapp_HEADERS})

if(USING_CONAN)
	vcmi_copy_conan_runtime_deps(vcmiwallpaper)
endif()

target_link_libraries(vcmiwallpaper PRIVATE vcmiclientcommon)

# Our own directory first, so that <StdInc.h> (the PCH) resolves to ours rather
# than client/StdInc.h, which we also inherit via vcmiclientcommon.
target_include_directories(vcmiwallpaper
	PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

# VCMI sets these with add_definitions(), which is directory-scoped and so does
# not reach a target defined outside the vcmi/ tree.
if(MSVC)
	target_compile_definitions(vcmiwallpaper PRIVATE
		_CRT_SECURE_NO_WARNINGS
		WINDOWS_IGNORE_PACKING_MISMATCH
	)
endif()

vcmi_set_output_dir(vcmiwallpaper "")
enable_pch(vcmiwallpaper)

install(TARGETS vcmiwallpaper DESTINATION ${BIN_DIR})
