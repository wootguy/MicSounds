#include "ChatSoundConverter.h"
#include "misc_utils.h"
#include <chrono>
#include <thread>
#include <string.h>

#undef read
#undef write
#undef close

using namespace std;

float g_packet_delay = 0.05f;

ChatSoundConverter::ChatSoundConverter(int playerIdx) {
	sampleRate = 12000; // opus allows: 8, 12, 16, 24, 48 khz
	frameDuration = 10; // opus allows: 2.5, 5, 10, 20, 40, 60, 80, 100, 120
	frameSize = (sampleRate / 1000) * frameDuration; // samples per frame
	framesPerPacket = 5; // 2 = minimum amount of frames for sven to accept packet
	packetDelay = frameDuration * framesPerPacket; // millesconds between output packets
	samplesPerPacket = frameSize * framesPerPacket;
	opusBitrate = 32000; // 32kbps = steam default

	readBuffer = new uint8_t[STREAM_BUFFER_SIZE];
	rateBufferIn = new float[STREAM_BUFFER_SIZE];
	rateBufferOut = new float[STREAM_BUFFER_SIZE];

	this->playerIdx = playerIdx;
	encoder = new SteamVoiceEncoder(frameSize, framesPerPacket, sampleRate, opusBitrate, OPUS_APPLICATION_AUDIO);

	thinkThread = NULL;
	#ifndef SINGLE_THREAD_MODE
		thinkThread = new thread(&ChatSoundConverter::think, this);
	#endif
}

ChatSoundConverter::~ChatSoundConverter() {
	exitSignal = true;

	if (thinkThread) {
		thinkThread->join();
		delete thinkThread;
	}
	delete encoder;

	delete[] readBuffer;
	delete[] rateBufferIn;
	delete[] rateBufferOut;
}

void ChatSoundConverter::play_samples() {
	if (listeners == 0) {
		return;
	}

	edict_t* plr = INDEXENT(playerIdx);

	if (!isValidPlayer(plr)) {
		return;
	}

	if (nextPacketTime > g_engfuncs.pfnTime()) {
		return;
	}

	VoicePacket packet;
	if (!outPackets.dequeue(packet)) {
		return;
	}

	if (packet.isNewSound) {
		packetNum = 0;
		playbackStartTime = g_engfuncs.pfnTime();
	}

	for (int i = 1; i <= gpGlobals->maxClients; i++) {
		edict_t* plr = INDEXENT(i);
		uint32_t plrBit = 1 << (i & 31);

		if (!isValidPlayer(plr)) {
			continue;
		}

		if ((listeners & plrBit) == 0) {
			continue;
		}

		bool reliablePackets = reliable & plrBit;
		int sendMode = reliablePackets ? MSG_ONE : MSG_ONE_UNRELIABLE;

		// svc_voicedata
		MESSAGE_BEGIN(sendMode, 53, NULL, plr);
		WRITE_BYTE(playerIdx-1); // entity which is "speaking"
		WRITE_SHORT(packet.size); // compressed audio length

		// Ideally, packet data would be written once and re-sent to whoever wants it.
		// However there's no way to change the message destination after creation.
		// Data is combined into as few chunks as possible to minimize the loops
		// needed to write the data. This optimization loops about 10% as much as 
		// writing bytes one-by-one for every player.

		// First, data is split into strings delimted by zeros. It can't all be one string
		// because a string can't contain zeros, and the data is not guaranteed to end with a 0.
		for (int k = 0; k < packet.sdata.size(); k++) {
			WRITE_STRING(packet.sdata[k].c_str()); // includes the null terminater
		}

		// ...but that can leave a large chunk of bytes at the end, so the remainder is
		// also combined into 32bit ints.
		for (int k = 0; k < packet.ldata.size(); k++) {
			WRITE_LONG(packet.ldata[k]);
		}

		// whatever is left at this point will be less than 4 iterations.
		for (int k = 0; k < packet.data.size(); k++) {
			WRITE_BYTE(packet.data[k]);
		}

		MESSAGE_END();
	}

	packetNum++;
	nextPacketTime = playbackStartTime + packetNum * (g_packet_delay - 0.0001f); // slightly fast to prevent mic getting quiet/choppy
	//println("Play %d", packetNum);

	if (nextPacketTime < g_engfuncs.pfnTime()) {
		play_samples();
	}
}

float calcNextPacketDelay(float playback_start_time, float packetNum) {
	float serverTime = g_engfuncs.pfnTime();
	float ideal_next_packet_time = playback_start_time + packetNum * (g_packet_delay - 0.0001f); // slightly fast to prevent mic getting quiet/choppy
	return (ideal_next_packet_time - serverTime) - gpGlobals->frametime;
}

void ChatSoundConverter::handleCommand(string cmd) {
	vector<string> parts = splitString(cmd, "?");

	if (parts.size() < 4) {
		println("[MicSounds] Invalid command '%s'", cmd.c_str());
		return;
	}

	string fpath = parts[0];
	pitch = atoi(parts[1].c_str());
	volume = atoi(parts[2].c_str());
	steamid64 = strtoull(parts[3].c_str(), NULL, 10);

	if (soundFile) {
		fclose(soundFile);
		soundFile = NULL;
	}

	soundFile = open_sound_file(fpath);

	if (!soundFile) {
		errors.enqueue(UTIL_VarArgs("[MicSounds] ERROR: Failed to open file: %s\n", fpath.c_str()));
		return;
	}

	if (fread(&wavHdr, sizeof(wav_hdr), 1, soundFile) != 1
		|| strncmp((char*)wavHdr.RIFF, "RIFF", 4) || strncmp((char*)wavHdr.WAVE, "WAVE", 4)) {
		errors.enqueue(UTIL_VarArgs("[MicSounds] ERROR: Invalid wav header: %s\n", fpath.c_str()));
		fclose(soundFile);
		soundFile = NULL;
		return;
	}

	if (wavHdr.bitsPerSample != 8 || wavHdr.NumOfChan != 1) {
		errors.enqueue(UTIL_VarArgs("[MicSounds] ERROR: Expected 8-bit mono samples: %s\n", fpath.c_str()));
		fclose(soundFile);
		soundFile = NULL;
		return;
	}

	resampler.setup(wavHdr.SamplesPerSec, sampleRate, 1, 32);
	resampler.inp_data = rateBufferIn;
	resampler.inp_count = 0;
	resampler.out_count = STREAM_BUFFER_SIZE;
	resampler.out_data = rateBufferOut;
	encodeBuffer.clear();
	startingNewSound = true;
	encoder->reset();
}

void ChatSoundConverter::think() {
	
#ifdef SINGLE_THREAD_MODE
	if (!exitSignal) {
#else
	while (!exitSignal) {
		this_thread::sleep_for(chrono::milliseconds(50));
#endif
		string cmd;
		if (commands.dequeue(cmd)) {
			handleCommand(cmd);
		}

		while (!exitSignal && (outPackets.size() < IDEAL_BUFFER_SIZE) && (soundFile || encodeBuffer.size())) {
			write_output_packet();
		}
	}
}

bool ChatSoundConverter::read_samples() {
	if (!soundFile) {
		return false;
	}

	int outputSampleCount = 0;

	float scale = (float)volume / 100.0f;

	if (resampler.inp_count == 0) {
		// resampler ran out of input data
		uint32_t readSamples = fread(readBuffer, 1, STREAM_BUFFER_SIZE, soundFile);

		if (readSamples == 0) {
			//println("Reached end of sound file");
			fclose(soundFile);
			soundFile = NULL;
			outputSampleCount = STREAM_BUFFER_SIZE - resampler.out_count;
		}

		for (int i = 0; i < readSamples; i++) {
			int16_t i16 = ((int16_t)readBuffer[i] - 128) * 256;
			rateBufferIn[i] = ((float)i16 / 32768.0f) * scale;
		}

		resampler.inp_count = readSamples;
		resampler.inp_data = rateBufferIn;
		//println("Refilled input buffer with %d samples", readSamples);
	}

	if (resampler.out_count == 0) {
		// completely filled output buffer
		resampler.out_count = STREAM_BUFFER_SIZE;
		resampler.out_data = rateBufferOut;
		outputSampleCount = STREAM_BUFFER_SIZE;
	}

	// TODO: pitch + volume
	if (outputSampleCount) {
		if (pitch != 100) {
			float speed = (float)pitch / 100;			
			float samplesPerStep = (float)sampleRate / (float)sampleRate*speed;
			int numSamplesNew = (float)outputSampleCount / samplesPerStep;
			float t = 0;

			for (int i = 0; i < numSamplesNew; i++) {
				int newIdx = t;
				encodeBuffer.push_back(clampf(rateBufferOut[newIdx], -1.0f, 1.0f) * 31767.0f);
				t += samplesPerStep;
			}
		}
		else {
			for (int i = 0; i < outputSampleCount; i++) {
				encodeBuffer.push_back(clampf(rateBufferOut[i], -1.0f, 1.0f) * 31767.0f);
			}
		}
	}

	if (!soundFile) {
		return false;
	}

	resampler.process();

	return true;
}

void ChatSoundConverter::write_output_packet() {
	while (encodeBuffer.size() < samplesPerPacket && read_samples()) ;

	if (encodeBuffer.size() == 0) {
		//println("No data to encode");
		return; // no data to write
	}
	else if (encodeBuffer.size() < samplesPerPacket) {
		// pad the buffer to fill a packet
		println("pad to fill opus packet");
		for (int i = encodeBuffer.size(); i < samplesPerPacket; i++) {
			encodeBuffer.push_back(0);
		}
	}

	vector<uint8_t> bytes = encoder->write_steam_voice_packet(&encodeBuffer[0], samplesPerPacket, steamid64);
	encodeBuffer.erase(encodeBuffer.begin(), encodeBuffer.begin() + samplesPerPacket);

	VoicePacket packet;
	packet.size = 0;

	string sdat = "";

	for (int x = 0; x < bytes.size(); x++) {
		byte bval = bytes[x];
		packet.data.push_back(bval);

		// combine into 32bit ints for faster writing later
		if (packet.data.size() == 4) {
			uint32 val = (packet.data[3] << 24) + (packet.data[2] << 16) + (packet.data[1] << 8) + packet.data[0];
			packet.ldata.push_back(val);
			packet.data.resize(0);
		}

		// combine into string for even faster writing later
		if (bval == 0) {
			packet.sdata.push_back(sdat);
			packet.ldata.resize(0);
			packet.data.resize(0);
			sdat = "";
		}
		else {
			sdat += (char)bval;
		}
	}

	packet.size = bytes.size();
	
	if (startingNewSound) {
		packet.isNewSound = true;
		startingNewSound = false;
	}

	//println("Write %d samples with %d left. %d packets in queue", samplesPerPacket, encodeBuffer.size(), outPackets.size());

	outPackets.enqueue(packet);
}

FILE* ChatSoundConverter::open_sound_file(string path) {
	static vector<const char*> search_paths = {
		"svencoop_addon/sound/",
		"svencoop/sound/",
		"svencoop_downloads/sound/",
	};

	for (int i = 0; i < search_paths.size(); i++) {
		string testPath = search_paths[i] + path;
		FILE* file = fopen(testPath.c_str(), "rb");
		if (file) {
			return file;
		}
	}

	return NULL;
}
