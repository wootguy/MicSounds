#pragma once
#include "extdll.h"
#include "util.h"

//#define SINGLE_THREAD_MODE

#define MAX_PLAYERS 32
#define println(fmt, ...) ALERT(at_console, fmt "\n", ##__VA_ARGS__)
#define logln(fmt, ...) ALERT(at_logged, fmt "\n", ##__VA_ARGS__)

struct PlayerInfo {
	Vector pos;
	bool connected;
	bool reliableMode; // only read/write by main thread
	bool globalMode;
	float volume;
};

extern volatile bool g_plugin_exiting;
extern bool g_admin_pause_packets;
extern PlayerInfo g_playerInfo[MAX_PLAYERS];
extern std::mutex playerInfoMutex;
extern volatile float g_attenuation;
extern volatile bool g_attenuation_enabled;

using namespace std;