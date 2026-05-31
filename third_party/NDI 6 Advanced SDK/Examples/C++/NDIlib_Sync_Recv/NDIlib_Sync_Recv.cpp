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
	static const std::chrono::high_resolution_clock::time_point no_time;
	std::chrono::high_resolution_clock::time_point last_audio_blip;
	std::chrono::high_resolution_clock::time_point last_video_blip;

	// Avoid detecting duplicate frames
	bool last_audio_was_blip = false;
	bool last_video_was_blip = false;

	// The descriptors
	NDIlib_video_frame_v2_t video_frame;
	NDIlib_audio_frame_v2_t audio_frame;

	// The current smoothed value
	float smoothed_average = 0.0f;

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
				if (is_blip(video_frame)) {	// If this was not a blip
					if (!last_video_was_blip) {	// This is the current video blip
						last_video_blip = current_time;

						// Display the AV latency
						if (last_audio_blip != no_time) {	// Get the time
							const int64_t microseconds = std::chrono::duration_cast<std::chrono::microseconds>(last_video_blip - last_audio_blip).count();

							// Get the smoothed average
							smoothed_average = smoothed_average ? (0.9f * smoothed_average + 0.1f * (float)microseconds) : microseconds;

							// Display the offset
							printf("Video - Audio offset = %1.2fms, average = %1.2fms\n", (float)microseconds / 1000.0f, (float)smoothed_average / 1000.0f);
							last_video_blip = last_audio_blip = no_time;
						}

						// If the last frame was a blip
						last_video_was_blip = true;
					}
				} else {	// Half a second in we always reset the time
					if ((last_video_blip != no_time) && (current_time - last_video_blip > std::chrono::milliseconds(500)))
						last_video_blip = no_time;

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
				if (blip_time >= 0) {	// If the last frame was a blip
					if (!last_audio_was_blip) {	// This is the current audio blip
						last_audio_blip = current_time + std::chrono::microseconds(((int64_t)blip_time * 1000000ll) / (int64_t)audio_frame.sample_rate);

						// Display the AV latency
						if (last_video_blip != no_time) {	// Get the time
							const int64_t microseconds = std::chrono::duration_cast<std::chrono::microseconds>(last_video_blip - last_audio_blip).count();

							// Get the smoothed average
							smoothed_average = smoothed_average ? (0.9f * smoothed_average + 0.1f * (float)microseconds) : microseconds;

							// Display the offset
							printf("Video - Audio offset = %1.2fms, average = %1.2fms\n", (float)microseconds / 1000.0f, (float)smoothed_average / 1000.0f);
							last_video_blip = last_audio_blip = no_time;
						}

						// It was a blip
						last_audio_was_blip = true;
					}
				} else {	// Half a second in we always reset the time
					if ((last_audio_blip != no_time) && (current_time - last_audio_blip > std::chrono::milliseconds(500)))
						last_audio_blip = no_time;

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

	// Not required, but nice
	NDIlib_destroy();

	// Finished
	return 0;
}
