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

    ADD_EXECUTABLE(main 
    WorldImporter/main.cpp)
    TARGET_LINK_LIBRARIES(main libWorldImporter SDL2-static zip zlibstatic)
endif(PROJECT_IS_TOP_LEVEL)

