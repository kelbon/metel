cmake_minimum_required(VERSION 3.20)
project(metel)

option(metel_ENABLE_TESTING "enables testing" OFF)
option(metel_ENABLE_ASAN "build ASAN (for CI)" OFF)
option(metel_ENABLE_UBSAN "build UBSAN (for CI)" OFF)

include(cmake/deps.cmake)

add_library(metellib STATIC
    # add new .cpp files here. GLOB for libraries is bad practice  
    src/common.cpp
)

target_include_directories(metellib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_link_libraries(metellib PUBLIC
    ${CMAKE_THREAD_LIBS_INIT}
    anyanylib
    kelcorolib
    zallib
)

set_target_properties(metellib PROPERTIES
	LINKER_LANGUAGE CXX
	CXX_STANDARD 20
	CMAKE_CXX_STANDARD_REQUIRED ON
)

if(metel_ENABLE_TESTING)
	include(CTest)
	add_subdirectory(test)
endif()

# generates launch.json, places compile_commands.json correctly for clangd
ccozy_on_configuration_end_vscode()
