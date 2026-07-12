# vcmi-map-wallpaper -- renders a Heroes III map as an animated wallpaper.
#
# This project embeds VCMI (the `vcmi/` submodule, a fork carrying a couple of
# small upstreamable patches) and builds one extra executable, `vcmiwallpaper`,
# on top of VCMI's client library. VCMI itself is unmodified apart from those
# patches; everything wallpaper-specific lives in src/.

cmake_minimum_required(VERSION 3.24)

project(VcmiMapWallpaper CXX)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vcmi/CMakeLists.txt")
	message(FATAL_ERROR
		"The vcmi/ submodule is empty. Populate it first:\n"
		"    git submodule update --init")
endif()

# VCMI's find_package() calls run in the vcmi/ directory scope, so the imported
# targets they create (SDL2::SDL2, Boost::*, ...) would not be visible when
# linking a target defined here, in a sibling directory. Promote them to global.
set(CMAKE_FIND_PACKAGE_TARGETS_GLOBAL ON)

# Must be decided before add_subdirectory(vcmi): VCMI FORCEs CMAKE_BUILD_TYPE to
# RelWithDebInfo if it is still unset by the time it is processed.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# We only need VCMI's client library. Everything else is dead weight -- and
# turning the Qt apps off is what lets this build without Qt at all.
#
# ENABLE_CLIENT stays ON: it is what builds vcmiclientcommon.
# ENABLE_DEBUG_CONSOLE stays ON (its default): on Windows it keeps the build in
# the console subsystem and stops SDL2main from hijacking main().
foreach(vcmiOption LAUNCHER EDITOR TRANSLATIONS DISCORD TEST MMAI)
	set(ENABLE_${vcmiOption} OFF CACHE BOOL "Disabled by vcmi-map-wallpaper" FORCE)
endforeach()

add_subdirectory(vcmi)

# Must come after add_subdirectory(vcmi): src/ calls VCMI's CMake helpers
# (vcmi_set_output_dir, enable_pch, assign_source_group, ...), which only exist
# once VCMI's cmake_modules have been included.
add_subdirectory(src)
