#include "main.h"
#include <string>
#include "mmlib.h"
#include "ChatSoundConverter.h"
#include "crc32.h"

using namespace std;

// Description of plugin
plugin_info_t Plugin_info = {
	META_INTERFACE_VERSION,	// ifvers
	"MicSounds",	// name
	"1.0",	// version
	__DATE__,	// date
	"w00tguy",	// author
	"https://github.com/wootguy/",	// url
	"MICSND",	// logtag, all caps please
	PT_ANYTIME,	// (when) loadable
	PT_ANYPAUSE,	// (when) unloadable
};

bool g_admin_pause_packets = false;
volatile bool g_plugin_exiting = false;
volatile float g_attenuation = 0.033f;
volatile bool g_attenuation_enabled = true;

ChatSoundConverter* g_soundConverters[MAX_PLAYERS];
PlayerInfo g_playerInfo[32];
mutex playerInfoMutex;

void PluginInit() {
	g_plugin_exiting = false;

	g_dll_hooks.pfnServerActivate = MapInit;
	g_dll_hooks.pfnStartFrame = StartFrame;
	g_dll_hooks.pfnClientDisconnect = ClientLeave;

	REG_SVR_COMMAND("play_mic_sound", mic_sound);
	REG_SVR_COMMAND("stop_mic_sound", stop_mic_sound);
	REG_SVR_COMMAND("config_mic_sound", config_mic_sound);
	REG_SVR_COMMAND("mic_sound_attn", mic_sound_attn);

	g_main_thread_id = std::this_thread::get_id();
	memset(g_playerInfo, 0, sizeof(PlayerInfo));

	for (int i = 0; i < MAX_PLAYERS; i++) {
		g_soundConverters[i] = new ChatSoundConverter(i+1);
		g_playerInfo[i].volume = 1.0f;
	}

	crc32_init();
}

void ClientLeave(edict_t* plr) {
	g_soundConverters[ENTINDEX(plr)-1]->listeners = 0;
	RETURN_META(MRES_IGNORED);
}

void MapInit(edict_t* pEdictList, int edictCount, int maxClients) {
	// reset reliable flags to prevent desyncs/overflows when joining the game
	// the angelscript plugin should reconfigure with a delay
	// also stop all sounds
	for (int i = 0; i < gpGlobals->maxClients; i++) {
		g_playerInfo[i].connected = false;
		g_soundConverters[i]->listeners = 0;
	}

	RETURN_META(MRES_IGNORED);
}

void mic_sound_attn() {
	CommandArgs args = CommandArgs();
	args.loadArgs();

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
}

void config_mic_sound() {
	CommandArgs args = CommandArgs();
	args.loadArgs();

	// bitfield indicated which players get reliable packets
	int playerIdx = atoi(args.ArgV(1).c_str());
	bool reliableMode = atoi(args.ArgV(2).c_str()) != 0;
	bool globalMode = atoi(args.ArgV(3).c_str()) != 0;
	float volume = atoi(args.ArgV(4).c_str()) / 100.0f;
	println("[MicSounds] config player %d %d %d %f", playerIdx, (int)reliableMode, (int)globalMode, volume);

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

void stop_mic_sound() {
	CommandArgs args = CommandArgs();
	args.loadArgs();

	int playerIdx = atoi(args.ArgV(1).c_str());
	bool stopForEveryone = atoi(args.ArgV(2).c_str()) > 0;

	if (playerIdx < 1 || playerIdx > gpGlobals->maxClients) {
		println("[MicSounds] Invalid client index");
		return;
	}

	if (stopForEveryone) {
		g_soundConverters[playerIdx - 1]->listeners = 0;
		println("[MicSounds] Stop sound from player %d", playerIdx);
	}
	else {
		uint32_t plrBit = 1 << (playerIdx & 31);
		println("[MicSounds] Stop sounds for player %d", playerIdx);

		for (int i = 0; i < gpGlobals->maxClients; i++) {
			g_soundConverters[i]->listeners &= ~plrBit;
		}
	}
}


// test command: play_mic_sound twlz/poney.wav 100 22 1 2
void mic_sound() {
	CommandArgs args = CommandArgs();
	args.loadArgs();

	string wantSound = args.ArgV(1);

	string fpath = args.ArgV(1);
	int pitch = atoi(args.ArgV(2).c_str());
	int volume = atoi(args.ArgV(3).c_str());
	int playerIdx = atoi(args.ArgV(4).c_str());
	uint32_t listeners = strtoul(args.ArgV(5).c_str(), NULL, 10);

	if (playerIdx < 1 || playerIdx > gpGlobals->maxClients) {
		println("[MicSounds] invalid player idx");
		return;
	}

	edict_t* plr = INDEXENT(playerIdx);

	if (!isValidPlayer(plr)) {
		println("[MicSounds] invalid player");
		return;
	}

	string steamid = (*g_engfuncs.pfnGetPlayerAuthId)(plr);
	uint64_t steamid64 = steamid64_min + playerIdx;

	if (steamid != "STEAM_ID_LAN" && steamid != "BOT") {
		steamid64 = steamid_to_steamid64(steamid);
	}

	string cmd = UTIL_VarArgs("%s?%d?%d?%llu", fpath.c_str(), pitch, volume, steamid64);
	int converterIdx = playerIdx - 1;

	g_soundConverters[converterIdx]->commands.enqueue(cmd);
	for (int i = 0; i < gpGlobals->maxClients; i++)
		g_soundConverters[converterIdx]->outPackets[i].clear();
	g_soundConverters[converterIdx]->listeners = listeners;

	println("[MicSounds] Play %s %d %d %u", fpath.c_str(), pitch, volume, listeners);

	return;
}

void StartFrame() {
	g_Scheduler.Think();
	handleThreadPrints();

	playerInfoMutex.lock();
	for (int i = 1; i <= gpGlobals->maxClients; i++) {
		edict_t* plr = INDEXENT(i);
		g_playerInfo[i-1].connected = isValidPlayer(plr);
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
			ClientPrintAll(HUD_PRINTNOTIFY, err.c_str());
		}
	}

	RETURN_META(MRES_IGNORED);
}

void PluginExit() {
	g_plugin_exiting = true;

	for (int i = 0; i < MAX_PLAYERS; i++) {
		g_soundConverters[i]->exitSignal = true;
	}

	println("Waiting for converter threads to join...");

	for (int i = 0; i < MAX_PLAYERS; i++) {
		delete g_soundConverters[i];
	}

	println("Plugin exit finish");
}
