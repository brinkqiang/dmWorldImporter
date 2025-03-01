cmake_minimum_required(VERSION 3.21)

project(WorldImporter)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(cmake/ModuleImport.cmake)
include(cmake/ModuleCompileOptions.cmake)
ModuleSetCompileOptions()
#ModuleSetWinCompilerFlags()

if(NOT WIN32)
    message(FATAL_ERROR "This project only supports Windows platforms.")
endif()

add_definitions(-DUNICODE -D_UNICODE -DZIP_STATIC)
ModuleImport("dmsdl2" "thirdparty/dmsdl2")
ModuleImport("libzip" "thirdparty/libzip")

include_directories("${CMAKE_CURRENT_SOURCE_DIR}/WorldImporter/include/nlohmann")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/WorldImporter/include")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/WorldImporter")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/libzip")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/libzip/lib")
include_directories("${CMAKE_BINARY_DIR}/thirdparty/libzip")

file(GLOB_RECURSE LIBWORLDIMPORTER_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/WorldImporter/*.cpp)

list(FILTER LIBWORLDIMPORTER_SOURCES EXCLUDE REGEX "main.cpp$")

add_library(libWorldImporter STATIC ${LIBWORLDIMPORTER_SOURCES})

target_link_libraries(libWorldImporter SDL2-static zip)

if (PROJECT_IS_TOP_LEVEL)

    add_executable(WorldImporter 
    WorldImporter/main.cpp)
    target_link_libraries(WorldImporter libWorldImporter)

	add_custom_command(
		TARGET WorldImporter
		PRE_BUILD  # 或 POST_BUILD（根据需求）
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			${CMAKE_CURRENT_SOURCE_DIR}/WorldImporter/config  # 源目录
			$<TARGET_FILE_DIR:WorldImporter>/config          # 目标目录
		COMMENT "Copying config directory to output..."
	)
endif(PROJECT_IS_TOP_LEVEL)

