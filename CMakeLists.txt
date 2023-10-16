cmake_minimum_required(VERSION 3.6)

project(MicSounds)

set(SOURCE_FILES
	src/main.h src/main.cpp
	src/ChatSoundConverter.h src/ChatSoundConverter.cpp
	src/SteamVoiceEncoder.h src/SteamVoiceEncoder.cpp
)

include_directories(include)
link_directories(lib)

add_library(${PROJECT_NAME} SHARED ${SOURCE_FILES})
add_subdirectory(mmlib)
mmlib_default_setup()
				
if(MSVC)
	# TODO: rebuild opus/zita-resampler libs so that this isn't needed
	set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MD")
	set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MDd")

	target_link_libraries(${PROJECT_NAME}
		${CMAKE_CURRENT_LIST_DIR}/lib/opus.lib
		${CMAKE_SOURCE_DIR}/lib/zita-resampler.lib
	)
else()	
	target_link_libraries(${PROJECT_NAME} -lopus -lpthread ${CMAKE_SOURCE_DIR}/lib/libzita-resampler.a)
endif()
