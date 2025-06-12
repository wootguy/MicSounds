#include "rehlds.h"
#include "ChatSoundConverter.h"
#include <chrono>
#include <thread>
#include <string.h>
#include "main.h"

#undef read
#undef write
#undef close

using namespace std;

float g_packet_delay = 0.05f;

ChatSoundConverter::ChatSoundConverter(int playerIdx) {
	sampleRate = 12000; // opus allows: 8, 12, 16, 24, 48 khz
	frameDuration = 20; // opus allows: 2.5, 5, 10, 20, 40, 60, 80, 100, 120
	frameSize = (sampleRate / 1000) * frameDuration; // samples per frame
	framesPerPacket = 5; // 2 = minimum amount of frames for sven to accept packet
	packetDelay = frameDuration * framesPerPacket; // millesconds between output packets
	samplesPerPacket = frameSize * framesPerPacket;
	opusBitrate = 32000; // 32kbps = steam default

	readBuffer = new uint8_t[STREAM_BUFFER_SIZE];
	rateBufferIn = new float[STREAM_BUFFER_SIZE];
	rateBufferOut = new float[STREAM_BUFFER_SIZE];
	volumeBuffer = new int16_t[samplesPerPacket];

	this->playerIdx = playerIdx;

	for (int i = 0; i < MAX_PLAYERS; i++) {
		encoder[i] = new SteamVoiceEncoder(frameSize, framesPerPacket, sampleRate, opusBitrate, OPUS_APPLICATION_AUDIO);
	}
}

ChatSoundConverter::~ChatSoundConverter() {
	for (int i = 0; i < MAX_PLAYERS; i++) {
		delete encoder[i];
	}

	if (soundFile) {
		fclose(soundFile);
		soundFile = NULL;
	}

	delete[] readBuffer;
	delete[] rateBufferIn;
	delete[] rateBufferOut;
	delete[] volumeBuffer;
}

void ChatSoundConverter::play_samples() {
	if (listeners == 0) {
		return;
	}

	edict_t* plr = INDEXENT(playerIdx);

	if (!IsValidPlayer(plr)) {
		listeners = 0;
		return;
	}

	if (nextPacketTime > g_engfuncs.pfnTime()) {
		return;
	}

	static VoicePacket packets[MAX_PLAYERS];

	if (outPackets[0].size() == 0) {
		return;
	}

	if (g_attenuation_enabled) {
		for (int i = 0; i < gpGlobals->maxClients; i++) {
			outPackets[i].dequeue(packets[i]);
		}
	}
	else {
		outPackets[0].dequeue(packets[0]);
	}

	if (packets[0].isNewSound) {
		packetNum = 0;
		playbackStartTime = g_engfuncs.pfnTime();
	}

	for (int i = 1; i <= gpGlobals->maxClients; i++) {
		edict_t* plr = INDEXENT(i);
		uint32_t plrBit = 1 << (i & 31);
		VoicePacket& packet = packets[g_attenuation_enabled ? i-1 : 0];

		if (!IsValidPlayer(plr)) {
			continue;
		}

		if ((listeners & plrBit) == 0) {
			continue;
		}

		if (packet.data.size() == 0) {
			continue;
		}

		bool reliablePackets = g_playerInfo[i-1].reliableMode;
		int sendMode = reliablePackets ? MSG_ONE : MSG_ONE_UNRELIABLE;

		rehlds_SendBigMessage(sendMode, SVC_VOICEDATA, &packet.data[0], packet.data.size(), i);
	}

	packetNum++;
	float fdelay = (packetDelay * 0.001f) - 0.0001f; // slightly fast to prevent mic getting quiet/choppy
	nextPacketTime = playbackStartTime + packetNum * fdelay;
	//println("Play %d", packetNum);

	if (nextPacketTime < g_engfuncs.pfnTime()) {
		play_samples();
	}
}

void ChatSoundConverter::clear() {
	VoicePacket packet;
	for (int i = 0; i < MAX_PLAYERS; i++) {
		outPackets[i].clear();
	}
	commands.clear();
	errors.clear();
}

void ChatSoundConverter::setQuality(int bitrate, int complexity) {
	for (int i = 0; i < gpGlobals->maxClients; i++) {
		encoder[i]->updateEncoderSettings(bitrate, complexity);
	}
}

void ChatSoundConverter::handleCommand(string cmd) {
	vector<string> parts = splitString(cmd, "?");

	if (parts.size() < 4) {
		println("Invalid command '%s'", cmd.c_str());
		return;
	}

	string fpath = parts[0];
	m_pitch = atoi(parts[1].c_str());
	m_volume = atoi(parts[2].c_str());
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

	int ret = parseWaveInfo(soundFile, wavHdr);
	if (ret) {
		errors.enqueue(UTIL_VarArgs("[MicSounds] ERROR: Invalid wav (error %d): %s\n", ret, fpath.c_str()));
		fclose(soundFile);
		soundFile = NULL;
		return;
	}

	if (wavHdr.bytesPerSample != 1 || wavHdr.channels != 1) {
		errors.enqueue(UTIL_VarArgs("[MicSounds] ERROR: Expected 8-bit mono samples: %s\n", fpath.c_str()));
		fclose(soundFile);
		soundFile = NULL;
		return;
	}

	readSamplesLeft = wavHdr.numSamples;
	fseek(soundFile, wavHdr.sampleFileOffset, SEEK_SET);

	resampler.setup(wavHdr.sampleRate, sampleRate, 1, 32);
	resampler.inp_data = rateBufferIn;
	resampler.inp_count = 0;
	resampler.out_count = STREAM_BUFFER_SIZE;
	resampler.out_data = rateBufferOut;
	encodeBuffer.clear();
	startingNewSound = true;
	for (int i = 0; i < MAX_PLAYERS; i++)
		encoder[i]->reset();
}

bool ChatSoundConverter::think() {
	string cmd;
	if (commands.dequeue(cmd)) {
		handleCommand(cmd);
	}

	while ((outPackets[0].size() < IDEAL_BUFFER_SIZE) && (soundFile || encodeBuffer.size())) {
		write_output_packet();
	}

	return outPackets->size();
}

bool ChatSoundConverter::read_samples() {
	if (!soundFile) {
		return false;
	}

	//static vector<int16_t> all_samples;

	int outputSampleCount = 0;

	float scale = (float)m_volume / 100.0f;

	if (resampler.inp_count == 0) {
		// resampler ran out of input data
		uint32_t readSamples = fread(readBuffer, 1, V_min(readSamplesLeft, STREAM_BUFFER_SIZE), soundFile);
		readSamplesLeft -= readSamples;

		//println("Read %d samples", readSamples);

		if (readSamples == 0) {
			//println("Reached end of sound file");
			int processed = resampler.process();
			fclose(soundFile);
			soundFile = NULL;
			outputSampleCount = STREAM_BUFFER_SIZE - resampler.out_count;
		}

		for (int i = 0; i < readSamples; i++) {
			int16_t i16 = ((int16_t)readBuffer[i] - 128) * 256;
			rateBufferIn[i] = ((float)i16 / 32768.0f) * scale;
		}

		//if (readSamples)
		//	writeWavFile("cs_test2.wav", readBuffer, readSamples, 22050, 1);

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
		if (m_pitch != 100) {
			float speed = (float)m_pitch / 100;
			float samplesPerStep = (float)sampleRate / (float)sampleRate*speed;
			int numSamplesNew = (float)outputSampleCount / samplesPerStep;
			float t = 0;

			for (int i = 0; i < numSamplesNew; i++) {
				int newIdx = V_min(t, outputSampleCount-1);
				encodeBuffer.push_back(clampf(rateBufferOut[newIdx], -1.0f, 1.0f) * 31767.0f);
				//all_samples.push_back(clampf(rateBufferOut[newIdx], -1.0f, 1.0f) * 31767.0f);
				t += samplesPerStep;
			}
			//writeWavFile("cs_test.wav", &all_samples[0], all_samples.size(), sampleRate, 2);
		}
		else {
			//println("Queue %d samples", outputSampleCount);
			for (int i = 0; i < outputSampleCount; i++) {
				encodeBuffer.push_back(clampf(rateBufferOut[i], -1.0f, 1.0f) * 31767.0f);
				//all_samples.push_back(clampf(rateBufferOut[i], -1.0f, 1.0f) * 31767.0f);
			}
			//writeWavFile("cs_test.wav", &all_samples[0], all_samples.size(), sampleRate, 2);
		}
	}

	if (!soundFile) {
		return false;
	}

	int oldin = resampler.inp_count;
	int oldout = resampler.out_count;
	int processed = resampler.process();
	//ALERT(at_console, "IN=%d -> %d, OUT=%d -> %d\n", oldin, resampler.inp_count, oldout, resampler.out_count);

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
		//println("pad to fill opus packet");
		for (int i = encodeBuffer.size(); i < samplesPerPacket; i++) {
			encodeBuffer.push_back(0);
		}
	}

	if (g_attenuation_enabled) {
		playerInfoMutex.lock();
		memcpy(playerInfoCopy, g_playerInfo, sizeof(PlayerInfo) * MAX_PLAYERS);
		playerInfoMutex.unlock();
	}

	Vector speakerPos = playerInfoCopy[playerIdx-1].pos;
	
	for (int i = 0; i < gpGlobals->maxClients; i++) {
		vector<uint8_t> voiceBytes;
		VoicePacket packet;

		if (startingNewSound) {
			packet.isNewSound = true;
			startingNewSound = false;
		}

		if (g_attenuation_enabled) {
			if (!playerInfoCopy[i].connected) {
				packet.data.clear();
				outPackets[i].enqueue(packet);
				encoder[i]->reset();
				continue;
			}

			float distance = (speakerPos - playerInfoCopy[i].pos).Length();
			float volume = max(0.0f, 1.0f - distance * g_attenuation*0.01f);
			volume = volume * volume * playerInfoCopy[i].volume; // exponential falloff

			if (m_volume > 100) {
				// need to greatly reduce volume for volume setting to have any effect on overdriven sounds
				volume *= 0.15f;
			}

			if (playerInfoCopy[i].globalMode) {
				volume = playerInfoCopy[i].volume;
			}

			if (volume < 0.01f || !playerInfoCopy[i].connected) {
				packet.data.clear();
				outPackets[i].enqueue(packet);
				encoder[i]->reset();
				continue;
			}

			memset(volumeBuffer, 0, sizeof(uint16_t) * samplesPerPacket);
			for (int k = 0; k < samplesPerPacket; k++) {
				volumeBuffer[k] = encodeBuffer[k] * volume;
			}

			voiceBytes = encoder[i]->write_steam_voice_packet(volumeBuffer, samplesPerPacket, steamid64);
		}
		else {
			voiceBytes = encoder[i]->write_steam_voice_packet(&encodeBuffer[0], samplesPerPacket, steamid64);
		}

		//println("Write %d samples with %d left. %d packets in queue", samplesPerPacket, encodeBuffer.size(), outPackets.size());

		int16_t voiceLen = voiceBytes.size();
		packet.data.push_back(playerIdx - 1);
		packet.data.push_back(voiceLen & 0xff);
		packet.data.push_back(voiceLen >> 8);
		packet.data.insert(packet.data.end(), voiceBytes.begin(), voiceBytes.end());
		outPackets[i].enqueue(packet);

		if (!g_attenuation_enabled) {
			break;
		}
	}

	encodeBuffer.erase(encodeBuffer.begin(), encodeBuffer.begin() + samplesPerPacket);
}

FILE* ChatSoundConverter::open_sound_file(string path) {
	static vector<const char*> search_paths = {
		"valve_addon/sound/",
		"valve/sound/",
		"valve_downloads/sound/",
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
