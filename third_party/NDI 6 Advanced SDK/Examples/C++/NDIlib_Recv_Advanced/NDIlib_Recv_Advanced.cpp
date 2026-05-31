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
		// Wait until the sources on the network have changed
		printf("Looking for sources ...\n");
		NDIlib_find_wait_for_sources(pNDI_find, 1000/* One second */);
		p_sources = NDIlib_find_get_current_sources(pNDI_find, &no_sources);
	}

	// Setup the NDI receiver to pass through compressed audio and video frames.
	NDIlib_recv_create_v3_t NDI_recv_create_desc = {};
	NDI_recv_create_desc.color_format = (NDIlib_recv_color_format_e)NDIlib_recv_color_format_compressed_v4_with_audio;

	// We now have at least one source, so we create a receiver to look at it.
	NDIlib_recv_instance_t pNDI_recv = NDIlib_recv_create_v3(&NDI_recv_create_desc);
	if (!pNDI_recv)
		return 0;

	// Connect to our sources
	NDIlib_recv_connect(pNDI_recv, p_sources + 0);

	// Destroy the NDI finder. We needed to have access to the pointers to p_sources[0]
	NDIlib_find_destroy(pNDI_find);

	// Run for one minute
	using namespace std::chrono;
	for (const auto start = high_resolution_clock::now(); high_resolution_clock::now() - start < minutes(1);) {
		// The descriptors
		NDIlib_video_frame_v2_t video_frame;
		NDIlib_audio_frame_v2_t audio_frame;
		NDIlib_metadata_frame_t metadata_frame;

		switch (NDIlib_recv_capture_v2(pNDI_recv, &video_frame, &audio_frame, &metadata_frame, 5000)) {
			// No data
			case NDIlib_frame_type_none:
				printf("No data received.\n");
				break;

			// Video data
			case NDIlib_frame_type_video:
				printf("Video data received (%dx%d).\n", video_frame.xres, video_frame.yres);
				if (video_frame.p_metadata) {
					// XML video metadata information.
					printf("XML video metadata %s.\n", video_frame.p_metadata);
				}
				if (video_frame.p_data) {
					// Video frame buffer.
					// Check video_frame.FourCC for the pixel format of this buffer
				}
				NDIlib_recv_free_video_v2(pNDI_recv, &video_frame);
				break;

			// Audio data
			case NDIlib_frame_type_audio:
				printf("Audio data received (%d samples).\n", audio_frame.no_samples);
				if (audio_frame.p_data) {
					// Audio frame buffer.
				}
				NDIlib_recv_free_audio_v2(pNDI_recv, &audio_frame);
				break;

			// Metadata
			case NDIlib_frame_type_metadata:
				if (metadata_frame.p_data) {
					// XML metadata information
					printf("Received metadata %s\n", metadata_frame.p_data);
				}
				NDIlib_recv_free_metadata(pNDI_recv, &metadata_frame);
				break;

			// There is a status change on the receiver
			case NDIlib_frame_type_status_change:
				printf("Receiver connection status changed.\n");
				break;
		}
	}

	// Destroy the receiver
	NDIlib_recv_destroy(pNDI_recv);

	// Not required, but nice
	NDIlib_destroy();

	// Finished
	return 0;
}
