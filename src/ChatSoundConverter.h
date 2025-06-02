#pragma once
#include "main.h"
#include <map>
#include <vector>
#include <string>
#include <thread>
#include "SteamVoiceEncoder.h"
#include "ThreadSafeQueue.h"
#include "zita-resampler/resampler.h"
#include "wav.h"

const uint64_t steamid64_min = 0x0110000100000001;
const uint64_t steamid64_max = 0x01100001FFFFFFFF;

#define IDEAL_BUFFER_SIZE 4
#define MAX_SOUND_SAMPLE_RATE 22050
#define STREAM_BUFFER_SIZE 16384 // buffer size for file read bytes and sample rate conversion buffers

//#define SINGLE_THREAD_MODE // for easier debugging

struct VoicePacket {
	bool isNewSound = false; // true if start of a new sound
	std::vector<uint8_t> data;

	VoicePacket() {}
};

struct ChatSoundConverter {
public:
	ThreadSafeQueue<VoicePacket> outPackets[MAX_PLAYERS];
	ThreadSafeQueue<std::string> commands;
	ThreadSafeQueue<std::string> errors;

	PlayerInfo playerInfoCopy[MAX_PLAYERS]; // thread copy of global data
	
	// only write these vars from main thread
	uint32_t listeners = 0xffffffff; // 1 bit = player is listener
	float playbackStartTime = 0;
	float nextPacketTime = 0;
	int packetNum = 0;

	ChatSoundConverter(int playerIdx);
	~ChatSoundConverter();

	void handleCommand(std::string cmd);

	void think();

	void play_samples(); // only access from main thread

	void clear();

private:
	// private vars only access from converter thread
	int sampleRate; // opus allows: 8, 12, 16, 24, 48 khz
	int frameDuration; // opus allows: 2.5, 5, 10, 20, 40, 60, 80, 100, 120
	int frameSize; // samples per frame
	int framesPerPacket; // 2 = minimum amount of frames for sven to accept packet
	int packetDelay; // millesconds between output packets
	int samplesPerPacket;
	int opusBitrate; // 32kbps = steam default
	int readSamplesLeft; // total samples left in the data chunk of the wav file
	Resampler resampler;

	FILE* open_sound_file(std::string path);

	// stream data from input wav file and do sample rate + pitch conversions
	// returns false for end of file
	bool read_samples();

	void write_output_packet(); // stream data from input wav file and write out a steam voice packet

	int playerIdx;
	uint64_t steamid64;
	int m_pitch;
	int m_volume;
	FILE* soundFile = NULL;
	WavInfo wavHdr;

	uint8_t* readBuffer;
	float* rateBufferIn;
	float* rateBufferOut;
	bool startingNewSound;
	int16_t* volumeBuffer;
	
	vector<int16_t> encodeBuffer; // samples ready to be encoded
	SteamVoiceEncoder* encoder[MAX_PLAYERS];
};