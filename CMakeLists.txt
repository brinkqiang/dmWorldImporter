cmake_minimum_required(VERSION 3.8)

PROJECT(WorldImporter)

LIST(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
INCLUDE(cmake/ModuleImport.cmake)
INCLUDE(cmake/ModuleCompileOptions.cmake)
ModuleSetCompileOptions()
#ModuleSetWinCompilerFlags()

add_definitions(-DUNICODE -D_UNICODE -DZIP_STATIC)
ModuleImport("dmsdl2" "thirdparty/dmsdl2")
ModuleImport("libzip" "thirdparty/libzip")

INCLUDE_DIRECTORIES("${CMAKE_CURRENT_SOURCE_DIR}/WorldImporter/include/nlohmann")
INCLUDE_DIRECTORIES("${CMAKE_CURRENT_SOURCE_DIR}/WorldImporter/include")
INCLUDE_DIRECTORIES("${CMAKE_CURRENT_SOURCE_DIR}/WorldImporter")
INCLUDE_DIRECTORIES("${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/libzip")
INCLUDE_DIRECTORIES("${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/libzip/lib")
INCLUDE_DIRECTORIES("${CMAKE_BINARY_DIR}/thirdparty/libzip")

ADD_LIBRARY(libWorldImporter STATIC
    WorldImporter/biome.cpp
	WorldImporter/block.cpp
	WorldImporter/blockstate.cpp
	WorldImporter/config.cpp
	WorldImporter/coord_conversion.cpp
	WorldImporter/dat.cpp
	WorldImporter/decompressor.cpp
	WorldImporter/EntityBlock.cpp
	WorldImporter/fileutils.cpp
	WorldImporter/GlobalCache.cpp
	WorldImporter/JarReader.cpp
	WorldImporter/model.cpp
	WorldImporter/nbtutils.cpp
	WorldImporter/objExporter.cpp
	WorldImporter/PointCloudExporter.cpp
	WorldImporter/RegionModelExporter.cpp
	WorldImporter/texture.cpp
	WorldImporter/version.cpp)

if (PROJECT_IS_TOP_LEVEL)

    ADD_EXECUTABLE(WorldImporter 
    WorldImporter/main.cpp)
    TARGET_LINK_LIBRARIES(WorldImporter libWorldImporter SDL2-static zip)

	ADD_CUSTOM_COMMAND(
		TARGET WorldImporter
		PRE_BUILD  # 或 POST_BUILD（根据需求）
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			${CMAKE_CURRENT_SOURCE_DIR}/WorldImporter/config  # 源目录
			$<TARGET_FILE_DIR:WorldImporter>/config          # 目标目录
		COMMENT "Copying config directory to output..."
	)
endif(PROJECT_IS_TOP_LEVEL)

