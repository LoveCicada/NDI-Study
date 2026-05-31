#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <atomic>
#include <chrono>
#include <Processing.NDI.Advanced.h>

#ifdef _WIN32
#ifdef _WIN64
#pragma comment(lib, "Processing.NDI.Lib.Advanced.x64.lib")
#else // _WIN64
#pragma comment(lib, "Processing.NDI.Lib.Advanced.x86.lib")
#endif // _WIN64
#endif

static std::atomic<bool> exit_loop(false);
static void sigint_handler(int)
{
	exit_loop = true;
}

int main(int argc, char* argv[])
{
	// Not required, but "correct" (see the SDK documentation).
	if (!NDIlib_initialize())
		return 0;

	// There must be a source parameter
	if (argc < 2)
		return 0;

	// Catch interrupt so that we can shut down gracefully
	signal(SIGINT, sigint_handler);

	// We now have at least one source, so we create a receiver to look at it.
	NDIlib_recv_create_v3_t create_settings;
	create_settings.color_format = NDIlib_recv_color_format_fastest;
	create_settings.bandwidth = NDIlib_recv_bandwidth_highest;
	create_settings.source_to_connect_to.p_ndi_name = argv[1];

	// Create the settings
	NDIlib_recv_instance_t pNDI_recv = NDIlib_recv_create_v3(&create_settings);
	if (!pNDI_recv)
		return 0;

	// The last audio time
	std::chrono::high_resolution_clock::time_point last_audio_time;
	std::chrono::high_resolution_clock::time_point last_video_time;

	// Keep track of the average times
	static const double average_smooth = 0.75;
	double video_time_average = 0.0, video_time_jitter = 0.0;
	double audio_time_average = 0.0, audio_time_jitter = 0.0;

	// The descriptors
	NDIlib_video_frame_v2_t video_frame;
	NDIlib_audio_frame_v2_t audio_frame;

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

				// Skip the first frame so we have an accurate measurement
				if (last_video_time != std::chrono::high_resolution_clock::time_point()) {
					// Get the time since the last sample.
					const int64_t microseconds = std::chrono::duration_cast<std::chrono::microseconds>(current_time - last_video_time).count();

					// Compute the average frame time
					const double smooth = (video_time_average == 0.0) ? 0.0 : average_smooth;
					video_time_average = smooth * video_time_average + (1.0 - smooth) * (double)microseconds;

					// Compute the average jitter
					const double jitter = std::abs((double)microseconds - video_time_average);
					video_time_jitter = smooth * video_time_jitter + (1.0 - smooth) * (double)jitter;

					// Output
					printf("Video, time=%1.2fms, jitter=%1.2fms\n", video_time_average / 1000.0, video_time_jitter / 1000.0);
				}

				// This is now the last time seen
				last_video_time = current_time;

				// Free the video packet
				NDIlib_recv_free_video_v2(pNDI_recv, &video_frame);
				break;
			}

			// Audio data
			case NDIlib_frame_type_audio:
			{
				// Get the current time
				const std::chrono::high_resolution_clock::time_point current_time = std::chrono::high_resolution_clock::now();

				// Skip the first frame so we have an accurate measurement
				if (last_audio_time != std::chrono::high_resolution_clock::time_point()) {
					// Get the time since the last sample.
					const int64_t microseconds = std::chrono::duration_cast<std::chrono::microseconds>(current_time - last_audio_time).count();

					// Compute the average frame time
					const double smooth = (audio_time_average == 0.0) ? 0.0 : average_smooth;
					audio_time_average = smooth * audio_time_average + (1.0 - smooth) * (double)microseconds;

					// Compute the average jitter
					const double jitter = std::abs((double)microseconds - audio_time_average);
					audio_time_jitter = smooth * audio_time_jitter + (1.0 - smooth) * (double)jitter;

					// Output
					printf("Audio, time=%1.2fms, jitter=%1.2fms\n", audio_time_average / 1000.0, audio_time_jitter / 1000.0);
				}

				// This is now the last time seen
				last_audio_time = current_time;

				// Free the audio packet
				NDIlib_recv_free_audio_v2(pNDI_recv, &audio_frame);
				break;
			}
		}
	}

	// Destroy the receiver
	NDIlib_recv_destroy(pNDI_recv);

	// Not required, but nice
	NDIlib_destroy();

	// Finished
	return 0;
}
