#pragma once
#include "meta_utils.h"

void MapInit(edict_t* pEdictList, int edictCount, int maxClients);
void StartFrame();
void mic_sound();
void stop_mic_sound();
void config_mic_sound();

extern volatile bool g_plugin_exiting;
extern bool g_admin_pause_packets;