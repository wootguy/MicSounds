#include "main.h"
#include "PluginHooks.h"
#include "PluginManager.h"
#include <string>
#include "ChatSoundConverter.h"
#include "CBasePlayer.h"
#include "Scheduler.h"
#include "crc32.h"

using namespace std;

bool g_admin_pause_packets = false;
volatile bool g_plugin_exiting = false;
volatile float g_attenuation = 0.033f;
volatile bool g_attenuation_enabled = true;

ChatSoundConverter* g_soundConverters[MAX_PLAYERS];
PlayerInfo g_playerInfo[32];
mutex playerInfoMutex;
std::thread* thinkThread = NULL;
volatile bool g_exitSignal;

HOOK_RETURN_DATA ClientLeave(CBasePlayer* plr) {
	g_soundConverters[plr->entindex() - 1]->listeners = 0;
	return HOOK_CONTINUE;
}

HOOK_RETURN_DATA MapInit() {
	// reset reliable flags to prevent desyncs/overflows when joining the game
	// the angelscript plugin should reconfigure with a delay
	// also stop all sounds
	for (int i = 0; i < gpGlobals->maxClients; i++) {
		g_playerInfo[i].connected = false;
		g_soundConverters[i]->listeners = 0;
	}

	return HOOK_CONTINUE;
}

bool mic_sound_attn(CBasePlayer* plr, const CommandArgs& args) {
	float attn = atof(args.ArgV(1).c_str());

	g_attenuation_enabled = attn > 0;
	g_attenuation = attn;

	if (g_attenuation_enabled) {
		println("MicSound Attenuation set to %f", g_attenuation);
		logln("MicSound Attenuation set to %f", g_attenuation);
	}
	else {
		println("MicSound Attenuation disabled");
		logln("MicSound Attenuation disabled");
	}

	return true;
}

APIFUNC void config_mic_sound(int playerIdx, bool reliableMode, bool globalMode, int vol) {
	float volume = vol / 100.0f;
	println("config player %d %d %d %f", playerIdx, (int)reliableMode, (int)globalMode, volume);

	int converterIdx = playerIdx - 1;
	if (converterIdx < 0 || converterIdx >= gpGlobals->maxClients) {
		println("Bad player index!");
		return;
	}	

	playerInfoMutex.lock();
	g_playerInfo[converterIdx].reliableMode = reliableMode;
	g_playerInfo[converterIdx].globalMode = globalMode;
	g_playerInfo[converterIdx].volume = volume;
	playerInfoMutex.unlock();
}

APIFUNC void stop_mic_sound(int playerIdx, bool stopForEveryone) {
	if (playerIdx < 1 || playerIdx > gpGlobals->maxClients) {
		println("Invalid client index");
		return;
	}

	if (stopForEveryone) {
		g_soundConverters[playerIdx - 1]->listeners = 0;
		println("Stop sound from player %d", playerIdx);
	}
	else {
		uint32_t plrBit = 1 << (playerIdx & 31);
		println("Stop sounds for player %d", playerIdx);

		for (int i = 0; i < gpGlobals->maxClients; i++) {
			g_soundConverters[i]->listeners &= ~plrBit;
		}
	}
}


// test command: play_mic_sound twlz/poney.wav 100 22 1 2
APIFUNC void play_mic_sound(const char* fpath, int pitch, int volume, int playerIdx, uint32_t listeners) {
	if (playerIdx < 1 || playerIdx > gpGlobals->maxClients) {
		println("invalid player idx");
		return;
	}

	edict_t* eplr = INDEXENT(playerIdx);

	if (!IsValidPlayer(eplr)) {
		println("invalid player");
		return;
	}

	string steamid = (*g_engfuncs.pfnGetPlayerAuthId)(eplr);
	uint64_t steamid64 = steamid64_min + playerIdx;

	if (steamid != "STEAM_ID_LAN" && steamid != "BOT") {
		steamid64 = steamid_to_steamid64(steamid.c_str());
	}

	string cmd = UTIL_VarArgs("%s?%d?%d?%llu", fpath, pitch, volume, steamid64);
	int converterIdx = playerIdx - 1;

	g_soundConverters[converterIdx]->commands.enqueue(cmd);
	for (int i = 0; i < gpGlobals->maxClients; i++)
		g_soundConverters[converterIdx]->outPackets[i].clear();
	g_soundConverters[converterIdx]->listeners = listeners;

	println("Play %s %d %d %u", fpath, pitch, volume, listeners);
}

void ChatSoundConverterThink() {
	while (!g_exitSignal) {
		this_thread::sleep_for(chrono::milliseconds(10));

		for (int i = 0; i < MAX_PLAYERS && !g_exitSignal; i++) {
			g_soundConverters[i]->think();
		}
	}
}

HOOK_RETURN_DATA StartFrame() {
	playerInfoMutex.lock();
	for (int i = 1; i <= gpGlobals->maxClients; i++) {
		edict_t* plr = INDEXENT(i);
		g_playerInfo[i-1].connected = IsValidPlayer(plr);
		g_playerInfo[i-1].pos = *(Vector*)&(plr->v.origin);
	}
	playerInfoMutex.unlock();
	
	for (int i = 0; i < MAX_PLAYERS; i++) {
#ifdef SINGLE_THREAD_MODE
		g_soundConverters[i]->think();
#endif
		g_soundConverters[i]->play_samples();

		string err;
		if (g_soundConverters[i]->errors.dequeue(err)) {
			UTIL_ClientPrintAll(print_console, err.c_str());
			ALERT(at_console, err.c_str());
		}
	}

	return HOOK_CONTINUE;
}

HLCOOP_PLUGIN_HOOKS g_hooks;

extern "C" int DLLEXPORT PluginInit() {
	g_plugin_exiting = false;

	g_hooks.pfnMapInit = MapInit;
	g_hooks.pfnStartFrame = StartFrame;
	g_hooks.pfnClientDisconnect = ClientLeave;

	RegisterPluginCommand("mic_sound_attn", mic_sound_attn, FL_CMD_SERVER);

	g_main_thread_id = std::this_thread::get_id();
	memset(g_playerInfo, 0, sizeof(PlayerInfo));

	for (int i = 0; i < MAX_PLAYERS; i++) {
		g_soundConverters[i] = new ChatSoundConverter(i + 1);
		g_playerInfo[i].volume = 1.0f;
	}

	thinkThread = new thread(&ChatSoundConverterThink);

	crc32_init();

	return RegisterPlugin(&g_hooks);
}

extern "C" void DLLEXPORT PluginExit() {
	g_plugin_exiting = true;
	g_exitSignal = true;

	println("Waiting for converter thread to join...");

	for (int i = 0; i < MAX_PLAYERS; i++) {
		delete g_soundConverters[i];
	}

	println("Plugin exit finish");
}
