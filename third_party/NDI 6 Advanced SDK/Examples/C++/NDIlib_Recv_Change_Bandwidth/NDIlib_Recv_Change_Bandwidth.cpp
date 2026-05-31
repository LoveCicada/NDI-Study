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
#endif // _WIN32

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

	// Create the receiver with video stream enabled to trigger a bandwidth change
	NDIlib_recv_create_v3_t NDI_recv_create_desc = {};
	NDI_recv_create_desc.bandwidth = NDIlib_recv_bandwidth_highest;

	// You will need a special vendor id to change the bandwidth on the receiver's video stream
	const char* p_ndi_config = R"({ "ndi": { "vendor": { "name": "CoolCo, inc.", "id": "ABCDEFG" }}})";

	// We now have at least one source, so we create a receiver to look at it.
	NDIlib_recv_instance_t pNDI_recv = NDIlib_recv_create_v4(&NDI_recv_create_desc, p_ndi_config);
	if (!pNDI_recv)
		return 0;

	// Connect to our sources
	NDIlib_recv_connect(pNDI_recv, p_sources + 0);

	// Destroy the NDI finder. We needed to have access to the pointers to p_sources[0]
	NDIlib_find_destroy(pNDI_find);

	// The descriptor
	NDIlib_video_frame_v2_t video_frame;

	bool low_bandwidth = true;
	auto start = std::chrono::high_resolution_clock::now();
	while (!exit_loop) {
		auto now = std::chrono::high_resolution_clock::now();
		auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start);

		// We want to trigger a bandwidth change every 5 seconds
		if (elapsed >= std::chrono::seconds(5)) {
			// Trigger a bandwidth change on the video stream
			if (NDIlib_recv_set_bandwidth(pNDI_recv, low_bandwidth ? NDIlib_recv_bandwidth_lowest : NDIlib_recv_bandwidth_highest)) {
				printf("Changed video stream's bandwidth to %s.\n", low_bandwidth ? "low" : "high");
			} else {
				printf("Failed to trigger the bandwidth change.\n");
			}

			// Reset the flags
			low_bandwidth = !low_bandwidth;

			// Reset the timer
			start = now;
		}

		switch (NDIlib_recv_capture_v2(pNDI_recv, &video_frame, nullptr, nullptr, 5000)) {
			// No data
			case NDIlib_frame_type_none:
				printf("No data received.\n");
				break;

			// Video data
			case NDIlib_frame_type_video:
				printf("Video data received (%dx%d).\n", video_frame.xres, video_frame.yres);
				NDIlib_recv_free_video_v2(pNDI_recv, &video_frame);
				break;

			// There is a status change on the receiver (e.g. new web interface).
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
