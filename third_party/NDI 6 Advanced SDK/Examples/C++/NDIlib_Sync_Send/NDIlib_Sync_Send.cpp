#include <cassert>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <atomic>
#include <Processing.NDI.Advanced.h>

#ifdef _WIN32
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

int main(int argc, char* argv[])
{
	// Not required, but "correct" (see the SDK documentation).
	if (!NDIlib_initialize()) {
		// Cannot run NDI. Most likely because the CPU is not sufficient (see SDK documentation).
		// you can check this directly with a call to NDIlib_is_supported_CPU()
		printf("Cannot run NDI.");
		return 0;
	}

	// Catch interrupt so that we can shut down gracefully
	signal(SIGINT, sigint_handler);

	// Create an NDI source that is called "My Video and Audio" and is clocked to the video.
	NDIlib_send_create_t NDI_send_create_desc;
	NDI_send_create_desc.clock_video = true;
	NDI_send_create_desc.clock_audio = false;
	NDI_send_create_desc.p_ndi_name = "Sync Source";

	// We create the NDI sender
	NDIlib_send_instance_t pNDI_send = NDIlib_send_create(&NDI_send_create_desc);
	if (!pNDI_send)
		return 0;

	// We are going to create a 1920x1080 interlaced frame at 29.97Hz.
	NDIlib_video_frame_v2_t NDI_video_frame;
	NDI_video_frame.xres = 1920;
	NDI_video_frame.yres = 1080;
	NDI_video_frame.FourCC = NDIlib_FourCC_type_UYVY;

	// Video format check
	if (argc > 1) {
		// Look at command line options
		for (int idx = 1; idx < argc; idx++) {
			// Get the video format
			const char* p_scan = argv[idx];

			// 4K
			if (!::strncasecmp(p_scan, "2160", 4))
				NDI_video_frame.xres = 3840, NDI_video_frame.yres = 2160, p_scan += 4;
			// 1080
			else if (!::strncasecmp(p_scan, "1080", 4))
				NDI_video_frame.xres = 1920, NDI_video_frame.yres = 1080, p_scan += 4;
			// 720
			else if (!::strncasecmp(p_scan, "720", 3))
				NDI_video_frame.xres = 1280, NDI_video_frame.yres = 720, p_scan += 3;
			// 576
			else if (!::strncasecmp(p_scan, "576", 3))
				NDI_video_frame.xres = 720, NDI_video_frame.yres = 576, p_scan += 3;
			// 480
			else if (!::strncasecmp(p_scan, "480", 3))
				NDI_video_frame.xres = 720, NDI_video_frame.yres = 480, p_scan += 3;
			// Basically empty
			else if (!::strncasecmp(p_scan, "NULL", 4))
				NDI_video_frame.xres = 16, NDI_video_frame.yres = 16, p_scan += 4;
			// Unknown format
			else
				continue;

			// Interlaced or progressive
			if (!::strncasecmp(p_scan, "p", 1))
				NDI_video_frame.frame_format_type = NDIlib_frame_format_type_progressive, p_scan += 1;
			else if (!::strncasecmp(p_scan, "i", 1))
				NDI_video_frame.frame_format_type = NDIlib_frame_format_type_interleaved, p_scan += 1;
			else
				continue;

			// The frame rates
			if (!::strncasecmp(p_scan, "60", 2))
				NDI_video_frame.frame_rate_N = 60000, NDI_video_frame.frame_rate_D = 1000;
			else if (!::strncasecmp(p_scan, "59", 2))
				NDI_video_frame.frame_rate_N = 60000, NDI_video_frame.frame_rate_D = 1001;
			else if (!::strncasecmp(p_scan, "50", 2))
				NDI_video_frame.frame_rate_N = 50000, NDI_video_frame.frame_rate_D = 1000;
			else if (!::strncasecmp(p_scan, "30", 2))
				NDI_video_frame.frame_rate_N = 30000, NDI_video_frame.frame_rate_D = 1000;
			else if (!::strncasecmp(p_scan, "29", 2))
				NDI_video_frame.frame_rate_N = 30000, NDI_video_frame.frame_rate_D = 1001;
			else if (!::strncasecmp(p_scan, "25", 2))
				NDI_video_frame.frame_rate_N = 30000, NDI_video_frame.frame_rate_D = 1200;
			else if (!::strncasecmp(p_scan, "24", 2))
				NDI_video_frame.frame_rate_N = 24000, NDI_video_frame.frame_rate_D = 1000;
			else if (!::strncasecmp(p_scan, "23", 2))
				NDI_video_frame.frame_rate_N = 24000, NDI_video_frame.frame_rate_D = 1001;
			else
				continue;
		}
	}

	printf(
		"Format = %dx%d %s at %1.2ffps\n", NDI_video_frame.xres, NDI_video_frame.yres,
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

		// Check an assumption below
		assert(audio_no_samples[idx] <= audio_no_samples[0]);
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

	// Not required, but nice
	NDIlib_destroy();

	// Finished
	return 0;
}
