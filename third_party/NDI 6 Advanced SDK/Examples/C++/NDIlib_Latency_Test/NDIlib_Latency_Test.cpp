#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <atomic>
#include <chrono>
#include <algorithm>
#include <string>
#include <thread>
#include <Processing.NDI.Advanced.h>

#ifdef _WIN32
#define strcasecmp  _stricmp
#define strncasecmp _strnicmp

#ifdef _WIN64
#pragma comment(lib, "Processing.NDI.Lib.Advanced.x64.lib")
#else // _WIN64
#pragma comment(lib, "Processing.NDI.Lib.Advanced.x86.lib")
#endif // _WIN64
#else
#include <strings.h>
#endif

static std::atomic<bool> exit_loop(false);
static void sigint_handler(int)
{
	exit_loop = true;
}

//***********************************************************************************************************
// This will put out blips 

// This is the last time a blip got put out
std::atomic<std::chrono::high_resolution_clock::duration> last_blip_time;

void ndi_output_blips(const char* p_format)
{
	// Create an NDI source that is called "My Video and Audio" and is clocked to the video.
	NDIlib_send_create_t NDI_send_create_desc;
	NDI_send_create_desc.clock_video = true;
	NDI_send_create_desc.clock_audio = false;
	NDI_send_create_desc.p_ndi_name = "Latency Source";

	// We create the NDI sender
	NDIlib_send_instance_t pNDI_send = NDIlib_send_create(&NDI_send_create_desc);
	if (!pNDI_send)
		return;

	// We are going to create a 1920x1080 interlaced frame at 29.97Hz.
	NDIlib_video_frame_v2_t NDI_video_frame;
	NDI_video_frame.xres = 1920;
	NDI_video_frame.yres = 1080;
	NDI_video_frame.FourCC = NDIlib_FourCC_type_UYVY;

	// If there is a format being specified
	if (p_format) {
		// 4K
		if (!strncasecmp(p_format, "2160", 4))
			NDI_video_frame.xres = 3840, NDI_video_frame.yres = 2160, p_format += 4;
		// 1080
		else if (!strncasecmp(p_format, "1080", 4))
			NDI_video_frame.xres = 1920, NDI_video_frame.yres = 1080, p_format += 4;
		// 720
		else if (!strncasecmp(p_format, "720", 3))
			NDI_video_frame.xres = 1280, NDI_video_frame.yres = 720, p_format += 3;
		// 576
		else if (!strncasecmp(p_format, "576", 3))
			NDI_video_frame.xres = 720, NDI_video_frame.yres = 576, p_format += 3;
		// 480
		else if (!strncasecmp(p_format, "480", 3))
			NDI_video_frame.xres = 720, NDI_video_frame.yres = 480, p_format += 3;
		// Basically empty
		else if (!strncasecmp(p_format, "NULL", 4))
			NDI_video_frame.xres = 16, NDI_video_frame.yres = 16, p_format += 4;

		// Interlaced of progressive
		if (!strncasecmp(p_format, "p", 1))
			NDI_video_frame.frame_format_type = NDIlib_frame_format_type_progressive, p_format += 1;
		else if (!strncasecmp(p_format, "i", 1))
			NDI_video_frame.frame_format_type = NDIlib_frame_format_type_interleaved, p_format += 1;

		// The frame-rates
		if (!strncasecmp(p_format, "60", 2))
			NDI_video_frame.frame_rate_N = 60000, NDI_video_frame.frame_rate_D = 1000;
		else if (!strncasecmp(p_format, "59", 2))
			NDI_video_frame.frame_rate_N = 60000, NDI_video_frame.frame_rate_D = 1001;
		else if (!strncasecmp(p_format, "50", 2))
			NDI_video_frame.frame_rate_N = 50000, NDI_video_frame.frame_rate_D = 1000;
		else if (!strncasecmp(p_format, "30", 2))
			NDI_video_frame.frame_rate_N = 30000, NDI_video_frame.frame_rate_D = 1000;
		else if (!strncasecmp(p_format, "29", 2))
			NDI_video_frame.frame_rate_N = 30000, NDI_video_frame.frame_rate_D = 1001;
		else if (!strncasecmp(p_format, "25", 2))
			NDI_video_frame.frame_rate_N = 30000, NDI_video_frame.frame_rate_D = 1200;
		else if (!strncasecmp(p_format, "24", 2))
			NDI_video_frame.frame_rate_N = 24000, NDI_video_frame.frame_rate_D = 1000;
		else if (!strncasecmp(p_format, "23", 2))
			NDI_video_frame.frame_rate_N = 24000, NDI_video_frame.frame_rate_D = 1001;
	}

	// Display the video format
	printf(
		"Source Format = %dx%d %s at %1.2ffps\n", NDI_video_frame.xres, NDI_video_frame.yres,
		(NDI_video_frame.frame_format_type == NDIlib_frame_format_type_progressive) ? "Progressive" : "Interlaced",
		(float)NDI_video_frame.frame_rate_N / (float)NDI_video_frame.frame_rate_D
	);

	// Allocate a pair of video frames, this allows us to do async sending.
	uint16_t* p_video_frame_white = new uint16_t[NDI_video_frame.xres * NDI_video_frame.yres];
	uint16_t* p_video_frame_black = new uint16_t[NDI_video_frame.xres * NDI_video_frame.yres];

	// Fill them in.
	std::fill_n(p_video_frame_white, NDI_video_frame.xres * NDI_video_frame.yres, 128 | (235 << 8));
	std::fill_n(p_video_frame_black, NDI_video_frame.xres * NDI_video_frame.yres, 128 | (16 << 8));

	// This is the stride.
	NDI_video_frame.line_stride_in_bytes = NDI_video_frame.xres * 2;

	// Create an audio buffer.
	NDIlib_audio_frame_v2_t NDI_audio_frame;
	NDI_audio_frame.sample_rate = 48000;
	NDI_audio_frame.no_channels = 2;

	// Because we want to generate the right sample numbers we generate a 5 item sequence.
	int audio_no_samples[5];
	for (int idx = 0, sample_no = 0; idx < 5; idx++) {
		// Get the samples
		const int sample_next = ((idx + 1) * NDI_audio_frame.sample_rate * NDI_video_frame.frame_rate_D + NDI_video_frame.frame_rate_N / 2) / NDI_video_frame.frame_rate_N;

		// Store the number of samples.
		audio_no_samples[idx] = sample_next - sample_no;
		sample_no = sample_next;
	}

	// An audio packet of a sine-wave.
	const int max_no_samples = audio_no_samples[0];
	float* p_audio_sine = new float[max_no_samples * NDI_audio_frame.no_channels];
	float* p_audio_silence = new float[max_no_samples * NDI_audio_frame.no_channels];

	// Fill out the buffers
	::memset(p_audio_silence, 0, sizeof(float) * max_no_samples * NDI_audio_frame.no_channels);

	// Get the sine values
	const float delta = 2 * 3.141592653589793f * 1000/*Hz*/ / (float)NDI_audio_frame.sample_rate;
	for (int idx = 0; idx < max_no_samples; idx++)
		p_audio_sine[idx] = cos((float)idx * delta);

	// Copy it into the next set of channels.
	for (int ch = 1; ch < NDI_audio_frame.no_channels; ch++)
		::memcpy(p_audio_sine + ch * max_no_samples, p_audio_sine, sizeof(float) * max_no_samples);

	// The channel stride
	NDI_audio_frame.channel_stride_in_bytes = sizeof(float) * max_no_samples;

	// We now loop forever
	const int sync_tone_period = (NDI_video_frame.frame_rate_N + NDI_video_frame.frame_rate_D / 2) / NDI_video_frame.frame_rate_D;
	for (int64_t idx = 0; !exit_loop; idx++) {
		// Set up the video frame
		switch (idx % sync_tone_period) {
			case 0:
				// White video, Blip !
				NDI_video_frame.p_data = (uint8_t*)p_video_frame_white;
				NDI_audio_frame.p_data = p_audio_sine;
				break;

			case 1:
				// White video, Blip !
				NDI_video_frame.p_data = (uint8_t*)p_video_frame_white;
				NDI_audio_frame.p_data = p_audio_silence;
				break;

			default:
				// Black video
				NDI_video_frame.p_data = (uint8_t*)p_video_frame_black;
				NDI_audio_frame.p_data = p_audio_silence;
				break;
		}

		// Send it async
		NDIlib_send_send_video_async_v2(pNDI_send, &NDI_video_frame);

		// Audio no samples
		NDI_audio_frame.no_samples = audio_no_samples[idx % 5];
		NDIlib_send_send_audio_v2(pNDI_send, &NDI_audio_frame);

		// Make this the current time
		last_blip_time = std::chrono::high_resolution_clock::now().time_since_epoch();
	}

	// Make sure we are finished sending
	NDIlib_send_send_video_async_v2(pNDI_send, nullptr);

	// Free the video frame
	delete[] p_video_frame_white;
	delete[] p_video_frame_black;
	delete[] p_audio_sine;
	delete[] p_audio_silence;

	// Destroy the NDI sender
	NDIlib_send_destroy(pNDI_send);
}

//***********************************************************************************************************
int is_blip(const NDIlib_audio_frame_v2_t& audio_frame)
{
	// Cycle across the channel and look for the first sample above 0.5
	for (int sample_no = 0; sample_no < audio_frame.no_samples; sample_no++)
		if (std::abs(audio_frame.p_data[sample_no] > 0.5f))
			return sample_no;

	// This is not a blip.
	return -1;
}

bool is_blip(const NDIlib_video_frame_v2_t& video_frame)
{
	// Cycle across the middle line
	const uint8_t* p_line = video_frame.p_data + (video_frame.yres / 2) * video_frame.line_stride_in_bytes;

	// Compute the average luminance
	int avg = 0;
	for (int x = 0; x < video_frame.xres; x++)
		avg += p_line[x * 2 + 1];

	// Get whether this looks white or not
	return ((avg / video_frame.xres) > 128);
}

void ndi_input_blips(const char* p_ndi_source)
{
	// We now have at least one source, so we create a receiver to look at it.
	NDIlib_recv_create_v3_t create_settings;
	create_settings.color_format = NDIlib_recv_color_format_fastest;
	create_settings.bandwidth = NDIlib_recv_bandwidth_highest;
	create_settings.source_to_connect_to.p_ndi_name = p_ndi_source;

	// Create the settings
	NDIlib_recv_instance_t pNDI_recv = NDIlib_recv_create_v3(&create_settings);
	if (!pNDI_recv)
		return;

	// Avoid detecting duplicate frames
	bool last_audio_was_blip = false;
	bool last_video_was_blip = false;

	// The descriptors
	NDIlib_video_frame_v2_t video_frame;
	NDIlib_audio_frame_v2_t audio_frame;

	// The current smoothed value
	float audio_smoothed_average = 0.0f;
	float video_smoothed_average = 0.0f;

	// Run for one minute	
	while (!exit_loop) {
		switch (NDIlib_recv_capture_v2(pNDI_recv, &video_frame, &audio_frame, nullptr, 5000)) {
			// No data
			case NDIlib_frame_type_none:
				printf("No data received.\n");
				break;

			// Video data
			case NDIlib_frame_type_video:
			{
				// Get the current time
				const std::chrono::high_resolution_clock::time_point current_time = std::chrono::high_resolution_clock::now();

				// Does this frame have a video blip
				if (is_blip(video_frame)) {
					// If the last frame was a blip
					if (!last_video_was_blip) {
						// Display the video latency
						const int64_t microseconds = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch() - last_blip_time.load()).count();

						// Display the offset
						printf("video latency = %1.2fms, average = %1.2fms\n", (float)microseconds / 1000.0f, (float)video_smoothed_average / 1000.0f);
						video_smoothed_average = video_smoothed_average ? (0.9f * video_smoothed_average + 0.1f * microseconds) : microseconds;

						// It was a blip
						last_video_was_blip = true;
					}
				} else {
					// No blip
					last_video_was_blip = false;
				}

				// Free the video packet
				NDIlib_recv_free_video_v2(pNDI_recv, &video_frame);
				break;
			}

			// Audio data
			case NDIlib_frame_type_audio:
			{
				// Get the current time
				const std::chrono::high_resolution_clock::time_point current_time = std::chrono::high_resolution_clock::now();

				// Does this frame have a blip
				const int blip_time = is_blip(audio_frame);
				if (blip_time >= 0) {
					// If the last frame was a blip
					if (!last_audio_was_blip) {
						// Display the video latency
						const int64_t microseconds = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch() - last_blip_time.load()).count();

						// Display the offset
						printf("Audio latency = %1.2fms, average = %1.2fms\n", (float)microseconds / 1000.0f, (float)audio_smoothed_average / 1000.0f);
						audio_smoothed_average = audio_smoothed_average ? (0.9f * audio_smoothed_average + 0.1f * microseconds) : microseconds;

						// It was a blip
						last_audio_was_blip = true;
					}
				} else {
					// It was a blip
					last_audio_was_blip = false;
				}

				// Free the audio packet
				NDIlib_recv_free_audio_v2(pNDI_recv, &audio_frame);
				break;
			}
		}
	}

	// Destroy the receiver
	NDIlib_recv_destroy(pNDI_recv);
}

//***********************************************************************************************************
// The main function
int main(int argc, char* argv[])
{
	// Not required, but "correct" (see the SDK documentation).
	if (!NDIlib_initialize())
		return 0;

	// Get the formats
	std::string video_format, video_source;
	for (int idx = 1; idx < argc - 1; idx++)
		if (0 == strcasecmp(argv[idx], "-f")) {
			// Get the format
			video_format = argv[++idx];
		} else if (0 == strcasecmp(argv[idx], "-i")) {
			// Get the source
			video_source = argv[++idx];
		}

		// Is there a command line of use ?
		if (video_source.empty()) {
			printf("Please specify a source name.\n");
			printf("	NDIlib_Latency_Test.exe -i \"MachineName (Sync Source)\"\n");
			printf("	NDIlib_Latency_Test.exe -f 1080p60 -i \"MachineName (Sync Source)\"\n");
			printf("	NDIlib_Latency_Test.exe -i \"MachineName (Sync Source)\" -f 2160p59.94\n");
			return 1;
		}

		// Catch interrupt so that we can shut down gracefully
		signal(SIGINT, sigint_handler);

		// Run two threads
		std::thread ndi_output(ndi_output_blips, video_format.c_str());
		std::thread ndi_input(ndi_input_blips, video_source.c_str());

		// Wait for things to exit
		ndi_output.join();
		ndi_input.join();

		// Not required, but nice
		NDIlib_destroy();

		// Finished
		return 0;
}
