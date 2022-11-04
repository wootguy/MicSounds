#include "main.h"
#include <string>
#include "enginecallback.h"
#include "eiface.h"
#include "misc_utils.h"
#include <algorithm>
#include <thread>
#include "ThreadSafeQueue.h"
#include "mstream.h"

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


void PluginInit() {
	g_plugin_exiting = false;

	g_dll_hooks.pfnStartFrame = StartFrame;

	REG_SVR_COMMAND("mic_sound", mic_sound);

	g_main_thread_id = std::this_thread::get_id();
}

void mic_sound() {
	CommandArgs args = CommandArgs();
	args.loadArgs();

	string wantSound = args.ArgV(1);

	println("Zomg play sound: %s", wantSound.c_str());

	return;
}

void handleThreadPrints() {
	string msg;
	for (int failsafe = 0; failsafe < 10; failsafe++) {
		if (g_thread_prints.dequeue(msg)) {
			println(msg.c_str());
		}
		else {
			break;
		}
	}

	for (int failsafe = 0; failsafe < 10; failsafe++) {
		if (g_thread_logs.dequeue(msg)) {
			logln(msg.c_str());
		}
		else {
			break;
		}
	}
}

void StartFrame() {
	g_Scheduler.Think();
	handleThreadPrints();
	RETURN_META(MRES_IGNORED);
}

void PluginExit() {
	g_plugin_exiting = true;
	//stop_network_threads();
	println("Plugin exit finish");
}
