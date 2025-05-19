cmake_minimum_required(VERSION 3.6)

project(MicSounds)

add_library(${PROJECT_NAME} SHARED
	src/main.h src/micsounds.cpp
	src/ChatSoundConverter.h src/ChatSoundConverter.cpp
	src/SteamVoiceEncoder.h src/SteamVoiceEncoder.cpp
	src/crc32.h src/crc32.cpp
)

include_directories(include)
link_directories(lib)

hlcoop_setup_plugin("plugins/server/")

if(MSVC)
	# TODO: rebuild opus/zita-resampler libs so that this isn't needed
	set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MD")
	set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MDd")

	target_link_libraries(${PROJECT_NAME} PRIVATE
		${CMAKE_CURRENT_LIST_DIR}/lib/opus.lib
		${CMAKE_CURRENT_LIST_DIR}/lib/zita-resampler.lib
	)
else()	
	target_link_libraries(${PROJECT_NAME} PRIVATE -lopus -lpthread ${CMAKE_CURRENT_LIST_DIR}/lib/libzita-resampler.a)
endif()
