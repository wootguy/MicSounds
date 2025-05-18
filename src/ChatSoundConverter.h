#pragma once
#include <extdll.h>
#include <map>
#include <vector>
#include <string>
#include <thread>
#include "SteamVoiceEncoder.h"
#include "ThreadSafeQueue.h"
#include "zita-resampler/resampler.h"
#include "main.h"

const uint64_t steamid64_min = 0x0110000100000001;
const uint64_t steamid64_max = 0x01100001FFFFFFFF;

#define IDEAL_BUFFER_SIZE 8
#define MAX_SOUND_SAMPLE_RATE 22050
#define STREAM_BUFFER_SIZE 32768 // buffer size for file read bytes and sample rate conversion buffers

//#define SINGLE_THREAD_MODE // for easier debugging

struct VoicePacket {
	uint32_t size = 0;
	bool isNewSound = false; // true if start of a new sound

	std::vector<std::string> sdata;
	std::vector<uint32_t> ldata;
	std::vector<uint8_t> data;

	VoicePacket() {}
};

typedef struct WAV_HEADER {
	/* RIFF Chunk Descriptor */
	uint8_t RIFF[4] = { 'R', 'I', 'F', 'F' }; // RIFF Header Magic header
	uint32_t ChunkSize;                     // RIFF Chunk Size
	uint8_t WAVE[4] = { 'W', 'A', 'V', 'E' }; // WAVE Header
	/* "fmt" sub-chunk */
	uint8_t fmt[4] = { 'f', 'm', 't', ' ' }; // FMT header
	uint32_t Subchunk1Size = 16;           // Size of the fmt chunk
	uint16_t AudioFormat = 1; // Audio format 1=PCM,6=mulaw,7=alaw,     257=IBM
								// Mu-Law, 258=IBM A-Law, 259=ADPCM
	uint16_t NumOfChan = 1;   // Number of channels 1=Mono 2=Sterio
	uint32_t SamplesPerSec = 12000;   // Sampling Frequency in Hz
	uint32_t bytesPerSec = 12000 * 2; // bytes per second
	uint16_t blockAlign = 2;          // 2=16-bit mono, 4=16-bit stereo
	uint16_t bitsPerSample = 16;      // Number of bits per sample
	/* "data" sub-chunk */
	uint8_t Subchunk2ID[4] = { 'd', 'a', 't', 'a' }; // "data"  string
	uint32_t Subchunk2Size;                        // Sampled data length
} wav_hdr;

struct ChatSoundConverter {
public:
	ThreadSafeQueue<VoicePacket> outPackets[MAX_PLAYERS];
	ThreadSafeQueue<std::string> commands;
	ThreadSafeQueue<std::string> errors;

	PlayerInfo playerInfoCopy[MAX_PLAYERS]; // thread copy of global data
	
	// only write these vars from main thread
	volatile bool exitSignal = false;
	uint32_t listeners = 0xffffffff; // 1 bit = player is listener
	float playbackStartTime = 0;
	float nextPacketTime = 0;
	int packetNum = 0;

	ChatSoundConverter(int playerIdx);
	~ChatSoundConverter();

	void handleCommand(std::string cmd);

	void think(); // don't call directly unless in single thread mode

	void play_samples(); // only access from main thread
	void calcNextPacketDelay();

private:
	// private vars only access from converter thread
	int sampleRate; // opus allows: 8, 12, 16, 24, 48 khz
	int frameDuration; // opus allows: 2.5, 5, 10, 20, 40, 60, 80, 100, 120
	int frameSize; // samples per frame
	int framesPerPacket; // 2 = minimum amount of frames for sven to accept packet
	int packetDelay; // millesconds between output packets
	int samplesPerPacket;
	int opusBitrate; // 32kbps = steam default
	Resampler resampler;

	FILE* open_sound_file(std::string path);

	// stream data from input wav file and do sample rate + pitch conversions
	// returns false for end of file
	bool read_samples();

	void write_output_packet(); // stream data from input wav file and write out a steam voice packet

	int playerIdx;
	uint64_t steamid64;
	int pitch;
	int volume;
	std::thread* thinkThread = NULL;
	FILE* soundFile = NULL;
	wav_hdr wavHdr;

	uint8_t* readBuffer;
	float* rateBufferIn;
	float* rateBufferOut;
	bool startingNewSound;
	int16_t* volumeBuffer;
	
	vector<int16_t> encodeBuffer; // samples ready to be encoded
	SteamVoiceEncoder* encoder[MAX_PLAYERS];
};