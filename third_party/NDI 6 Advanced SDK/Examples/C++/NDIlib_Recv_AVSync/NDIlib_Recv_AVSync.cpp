#include <cstdio>
#include <chrono>
#include <Processing.NDI.Advanced.h>

#ifdef _WIN32
#ifdef _WIN64
#pragma comment(lib, "Processing.NDI.Lib.Advanced.x64.lib")
#else // _WIN64
#pragma comment(lib, "Processing.NDI.Lib.Advanced.x86.lib")
#endif // _WIN64
#endif // _WIN32

int main(int argc, char* argv[])
{
	// Not required, but "correct" (see the SDK documentation).
	if (!NDIlib_initialize())
		return 0;

	// Create a finder
	NDIlib_find_instance_t pNDI_find = NDIlib_find_create_v2();
	if (!pNDI_find)
		return 0;

	// Wait until there is one source
	uint32_t no_sources = 0;
	const NDIlib_source_t* p_sources = NULL;
	while (!no_sources) {
		// Wait until the sources on the nwtork have changed
		printf("Looking for sources ...\n");
		NDIlib_find_wait_for_sources(pNDI_find, 1000/* One second */);
		p_sources = NDIlib_find_get_current_sources(pNDI_find, &no_sources);
	}

	// We now have at least one source, so we create a receiver to look at it.
	NDIlib_recv_instance_t pNDI_recv = NDIlib_recv_create_v3();
	if (!pNDI_recv)
		return 0;

	// Connect to our sources
	NDIlib_recv_connect(pNDI_recv, p_sources + 0);

	// Destroy the NDI finder. We needed to have access to the pointers to p_sources[0]
	NDIlib_find_destroy(pNDI_find);

	// Create an AV sync object
	NDIlib_avsync_instance_t pNDI_avsync = NDIlib_avsync_create(pNDI_recv);

	// Run for one minute
	using namespace std::chrono;
	for (const auto start = high_resolution_clock::now(); high_resolution_clock::now() - start < minutes(5);) {
		// Capture video or meta-data. Note that we do NOT capture audio here, that is 
		// done by the AVSync object
		NDIlib_video_frame_v2_t video_frame;
		switch (NDIlib_recv_capture_v2(pNDI_recv, &video_frame, nullptr, nullptr, 5000)) {
			case NDIlib_frame_type_none:
				printf("No data received.\n");
				break;

			case NDIlib_frame_type_video:
			{
				NDIlib_audio_frame_v3_t audio_frame;
				audio_frame.no_samples = 0;
				audio_frame.sample_rate = 0;
				if (NDIlib_avsync_synchronize(pNDI_avsync, &video_frame, &audio_frame) >= NDIlib_avsync_ret_success) {
					// We got video without audio
					printf(
						"Video data received (%dx%d, %1.2f Hz) with audio (%d samples, %d channels, %d Hz).\n",
						video_frame.xres, video_frame.yres, (float)(video_frame.frame_rate_N) / video_frame.frame_rate_D,
						audio_frame.no_samples, audio_frame.no_channels, audio_frame.sample_rate
					);

					// Free the audio that was received
					NDIlib_avsync_free_audio(pNDI_avsync, &audio_frame);
				} else {
					// We got video without audio
					printf(
						"Video data received (%dx%d, %1.2f Hz) without audio.\n",
						video_frame.xres, video_frame.yres, (float)(video_frame.frame_rate_N) / video_frame.frame_rate_D
					);
				}

				// Free the video
				NDIlib_recv_free_video_v2(pNDI_recv, &video_frame);
			}	break;
		}
	}

	// Destroy the AV sync object
	NDIlib_avsync_destroy(pNDI_avsync);

	// Destroy the receiver
	NDIlib_recv_destroy(pNDI_recv);

	// Not required, but nice
	NDIlib_destroy();

	// Finished
	return 0;
}
