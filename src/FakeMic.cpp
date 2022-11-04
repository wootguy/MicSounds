#include "FakeMic.h"
#include "misc_utils.h"
#include <errno.h>

uint32_t ideal_buffer_size = 8; // amount of packets to delay playback of. Higher = more latency + bad connection tolerance

float g_playback_start_time = 0;
float g_ideal_next_packet_time = 0;
int g_packet_idx = 0;
uint16 expectedNextPacketId = 0;

ThreadSafeQueue<VoicePacket> g_voice_data_stream;

const float BUFFER_DELAY = 0.7f; // minimum time for a voice packet to reach players (seems to actually be 4x longer...)

// using postThink hook instead of g_Scheduler so that music keeps playing during game_end screen
float g_next_play_samples = 0;
bool g_buffering_samples = false;

VoicePacket::VoicePacket(const VoicePacket& other) {
	this->id = other.id;
	this->size = other.size;
	this->sdata = other.sdata;
	this->ldata = other.ldata;
	this->data = other.data;
}

void FakeMicThink() {
	if (g_admin_pause_packets) {
		return;
	}
	float time = g_engfuncs.pfnTime();

	if (g_next_play_samples != -1 && time >= g_next_play_samples) {
		g_next_play_samples = -1;
		play_samples();
	}
}

void play_samples() {}
