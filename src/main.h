#pragma once
#include "meta_utils.h"

void StartFrame();
void mic_sound();

extern volatile bool g_plugin_exiting;
extern bool g_admin_pause_packets;