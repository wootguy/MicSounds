cmake_minimum_required(VERSION 3.6)

set(SVEN_ROOT_PATH "C:/Games/Steam/steamapps/common/Sven Co-op/" CACHE FILEPATH "Where your Sven Co-op folder is")
set(DLL_INSTALL_PATH "svencoop/addons/metamod/dlls" CACHE FILEPATH "Where to install the DLL (relative to Sven Co-op folder)")

project(MicSounds)

set(SOURCE_FILES
	src/main.h src/main.cpp
	src/misc_utils.h src/misc_utils.cpp
	src/meta_utils.h src/meta_utils.cpp
	src/meta_init.h src/meta_init.cpp
	src/Scheduler.h src/Scheduler.cpp
	src/ChatSoundConverter.h src/ChatSoundConverter.cpp
	src/SteamVoiceEncoder.h src/SteamVoiceEncoder.cpp
	src/crc32.h src/crc32.cpp
	src/EHandle.h src/EHandle.cpp
	src/ThreadSafeQueue.h src/ThreadSafeQueue.cpp
	src/mstream.h src/mstream.cpp
	src/vectors.h src/vectors.cpp
)

include_directories(src)
include_directories(include)
include_directories(include/metamod)
include_directories(include/hlsdk/common)
include_directories(include/hlsdk/dlls)
include_directories(include/hlsdk/engine)
include_directories(include/hlsdk/pm_shared)
link_directories(lib)

add_library(${PROJECT_NAME} SHARED ${SOURCE_FILES})

set_target_properties(${PROJECT_NAME} PROPERTIES
                      RUNTIME_OUTPUT_DIRECTORY_DEBUG "${SVEN_ROOT_PATH}/${DLL_INSTALL_PATH}"
                      RUNTIME_OUTPUT_DIRECTORY_RELEASE "${SVEN_ROOT_PATH}/${DLL_INSTALL_PATH}")
				
if(MSVC)											
	set_target_properties(${PROJECT_NAME} PROPERTIES
					  VS_STARTUP_PROJECT ${PROJECT_NAME}
					  VS_DEBUGGER_COMMAND "svencoop.exe"
					  VS_DEBUGGER_COMMAND_ARGUMENTS "+map stadium4 -nomsaa -nofbo -noborder -condebug"
					  VS_DEBUGGER_WORKING_DIRECTORY "${SVEN_ROOT_PATH}")
	
	# can't overwrite dll while its loaded but it can be renamed, then you can run "meta reload <plugin>" to update it in-game				
	add_custom_command(TARGET ${PROJECT_NAME}  PRE_BUILD
		COMMAND ${CMAKE_CURRENT_LIST_DIR}/replace_dll.bat
	)
	
	target_link_libraries(${PROJECT_NAME}
		${CMAKE_CURRENT_LIST_DIR}/lib/opus.lib
		${CMAKE_SOURCE_DIR}/lib/zita-resampler.lib
	)

else()
	target_compile_definitions(${PROJECT_NAME} PUBLIC -Dlinux)
	set(CMAKE_CXX_FLAGS "-Wall -std=c++11 -m32 -g")
	set(CMAKE_CXX_FLAGS_DEBUG "-O0")
	set(CMAKE_CXX_FLAGS_RELEASE "-Os -w -Wfatal-errors")
	
	target_link_libraries(${PROJECT_NAME} -lopus -lpthread ${CMAKE_SOURCE_DIR}/lib/libzita-resampler.a)
endif()
