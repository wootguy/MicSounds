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
	g_soundConverters[plr->entindex() - 1]->specListener = false;
	return HOOK_CONTINUE;
}

HOOK_RETURN_DATA MapInit() {
	// reset reliable flags to prevent desyncs/overflows when joining the game
	// the angelscript plugin should reconfigure with a delay
	// also stop all sounds
	playerInfoMutex.lock();
	for (int i = 0; i < gpGlobals->maxClients; i++) {
		g_playerInfo[i].connected = false;
		g_soundConverters[i]->listeners = 0;
		g_soundConverters[i]->specListener = false;
		g_soundConverters[i]->emitterEnt = 0;
	}
	playerInfoMutex.unlock();

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
		g_soundConverters[playerIdx - 1]->specListener = false;
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


// steam id written in the voice packets of a player slot. Slots without one get a fake id from the index.
static uint64_t voice_steamid64(int playerIdx) {
	edict_t* eplr = INDEXENT(playerIdx);

	string steamid = (*g_engfuncs.pfnGetPlayerAuthId)(eplr);
	uint64_t steamid64 = steamid64_min + playerIdx;

	if (steamid != "STEAM_ID_LAN" && steamid != "BOT" && steamid != "UNKNOWN") {
		steamid64 = steamid_to_steamid64(steamid.c_str());
	}

	return steamid64;
}

static void start_mic_sound(const char* fpath, int pitch, int volume, int playerIdx, int fakePlayerIdx,
	edict_t* emitter, uint64_t emitterSteamid64, uint32_t listeners) {
	uint64_t steamid64 = voice_steamid64(playerIdx);

	string cmd = UTIL_VarArgs("%s?%d?%d?%llu?%llu", fpath, pitch, volume, steamid64, emitterSteamid64);
	int converterIdx = clampi(playerIdx - 1, 0, MAX_PLAYERS-1);

	ChatSoundConverter* converter = g_soundConverters[converterIdx];

	converter->fakePlayerIdx = fakePlayerIdx;

	playerInfoMutex.lock();
	converter->emitterEnt = emitter ? ENTINDEX(emitter) : 0;
	converter->emitterSerial = emitter ? emitter->serialnumber : 0;
	if (emitter) {
		converter->emitterPos = emitter->v.origin;
	}
	playerInfoMutex.unlock();

	converter->commands.enqueue(cmd);
	for (int i = 0; i < gpGlobals->maxClients; i++)
		converter->outPackets[i].clear();
	converter->specPackets.clear();
	converter->listeners = listeners;
	converter->specListener = true; // HLTV viewers hear every chat sound and mute them themselves

	println("Play %s %d %d %u", fpath, pitch, volume, listeners);
}

// test command: play_mic_sound twlz/poney.wav 100 22 1 2
APIFUNC void play_mic_sound(const char* fpath, int pitch, int volume, int playerIdx, int fakePlayerIdx, uint32_t listeners) {
	if (playerIdx < 1 || playerIdx > gpGlobals->maxClients) {
		println("invalid player idx");
		return;
	}

	// The spectator copy says who emitted the sound, which is a different player than the slot it
	// plays from when fakePlayerIdx is set, so the HLTV demo player can fade it by distance and
	// apply that player's mute. Players' copies keep the slot's id.
	bool validFake = fakePlayerIdx > 0 && fakePlayerIdx <= gpGlobals->maxClients;
	uint64_t emitterSteamid64 = voice_steamid64(validFake ? fakePlayerIdx : playerIdx);

	start_mic_sound(fpath, pitch, volume, playerIdx, fakePlayerIdx, NULL, emitterSteamid64, listeners);
}

// Plays a sound that comes from an entity which isn't a player, through an empty player slot. Used
// for the chat sounds of HLTV web viewers: the slot is the one that shows the viewer on the
// scoreboard, and the entity is their camera, which players hear the sound fade out from. The
// spectator copy has emitterSteamid64, the viewer's Steam ID, which the HLTV plugin lists with their
// camera so the demo player can do the same. The sound stops if the entity is removed or a player
// takes the slot.
APIFUNC void play_mic_sound_from_entity(const char* fpath, int pitch, int volume, int slotIdx, int emitterEnt,
	uint64_t emitterSteamid64, uint32_t listeners) {
	if (slotIdx < 1 || slotIdx > gpGlobals->maxClients || UTIL_PlayerByIndex(slotIdx)) {
		println("invalid or occupied slot");
		return;
	}

	edict_t* emitter = emitterEnt > 0 && emitterEnt < gpGlobals->maxEntities ? INDEXENT(emitterEnt) : NULL;
	if (!emitter || emitter->free) {
		println("invalid emitter entity");
		return;
	}

	start_mic_sound(fpath, pitch, volume, slotIdx, 0, emitter, emitterSteamid64, listeners);
}

void ChatSoundConverterThink() {
	int oldActiveEncoders = 0;

	while (!g_exitSignal) {
		this_thread::sleep_for(chrono::milliseconds(10));

		int activeEncoders = 0;

		for (int i = 0; i < MAX_PLAYERS && !g_exitSignal; i++) {
			if (g_soundConverters[i]->think()) {
				activeEncoders++;
			}
		}

		if (activeEncoders <= 0)
			activeEncoders = 1;

		if (activeEncoders != oldActiveEncoders) {
			int newBitrate = 32000;
			int newComplexity = 0;

			if (activeEncoders >= 4) {
				newBitrate = 8000;
				newComplexity = 10;
			}
			else if (activeEncoders >= 3) {
				newBitrate = 12000;
				newComplexity = 10;
			} else if (activeEncoders >= 2) {
				newBitrate = 16000;
				newComplexity = 0;
			}

			//ALERT(at_console, "New encoder settings: %dbps %d complexity\n", newBitrate, newComplexity);

			for (int i = 0; i < MAX_PLAYERS; i++) {
				g_soundConverters[i]->setQuality(newBitrate, newComplexity);
			}
		}

		oldActiveEncoders = activeEncoders;
	}
}

HOOK_RETURN_DATA StartFrame() {
	playerInfoMutex.lock();
	for (int i = 1; i <= gpGlobals->maxClients; i++) {
		edict_t* plr = INDEXENT(i);
		if (plr) {
			g_playerInfo[i - 1].connected = IsValidPlayer(plr);
			g_playerInfo[i - 1].pos = *(Vector*)&(plr->v.origin);
		}
	}
	for (int i = 0; i < MAX_PLAYERS; i++) {
		ChatSoundConverter* converter = g_soundConverters[i];
		edict_t* emitter = converter->emitterEnt ? INDEXENT(converter->emitterEnt) : NULL;
		if (emitter && !emitter->free && emitter->serialnumber == converter->emitterSerial) {
			converter->emitterPos = emitter->v.origin;
		}
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
