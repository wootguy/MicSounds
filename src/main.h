#pragma once
#include "meta_utils.h"
#include "misc_utils.h"

void MapInit(edict_t* pEdictList, int edictCount, int maxClients);
void StartFrame();
void mic_sound();
void ClientLeave(edict_t* plr);
void stop_mic_sound();
void config_mic_sound();
void toggle_mic_sound_mode();
void mic_sound_attn();

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