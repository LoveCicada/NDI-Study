#include <cstdio>
#include <chrono>
#include <thread>
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

	// We now have at least one source, so we create a receiver to look at it.
	NDIlib_recv_create_v3_t NDI_recv_create_desc;
	NDI_recv_create_desc.source_to_connect_to = p_sources[0];
	NDI_recv_create_desc.color_format = (NDIlib_recv_color_format_e)NDIlib_recv_color_format_compressed_v2;

	// A high quality receiver
	NDIlib_recv_instance_t pNDI_recv_highQ = NDIlib_recv_create_v3(&NDI_recv_create_desc);
	if (!pNDI_recv_highQ)
		return 0;

	// A low quality receiver
	NDI_recv_create_desc.bandwidth = NDIlib_recv_bandwidth_lowest;
	NDIlib_recv_instance_t pNDI_recv_lowQ = NDIlib_recv_create_v3(&NDI_recv_create_desc);
	if (!pNDI_recv_lowQ)
		return 0;

	// Create an NDI source that is called "My Video and Audio" and is clocked to the video.
	NDIlib_send_create_t NDI_send_create_desc;
	NDI_send_create_desc.clock_video = NDI_send_create_desc.clock_audio = false;
	NDI_send_create_desc.p_ndi_name = "Forward";

	// We create the NDI sender
	NDIlib_send_instance_t pNDI_send = NDIlib_send_create(&NDI_send_create_desc);
	if (!pNDI_send)
		return 0;

	// This will forward video
	auto forward_video_highQ_fcn = [=](bool& exit) {
		while (!exit) {
			// Get the video frame
			NDIlib_video_frame_v2_t video_frame;
			if (NDIlib_recv_capture_v2(pNDI_recv_highQ, &video_frame, nullptr, nullptr, 1000) == NDIlib_frame_type_video) {
				// We can send it async, allowing us to capture right away
				::NDIlib_send_send_video_v2(pNDI_send, &video_frame);
				::NDIlib_recv_free_video_v2(pNDI_recv_highQ, &video_frame);
			}
		}
	};

	auto forward_video_lowQ_fcn = [=](bool& exit) {
		while (!exit) {
			// Get the video frame
			NDIlib_video_frame_v2_t video_frame;
			if (NDIlib_recv_capture_v2(pNDI_recv_lowQ, &video_frame, nullptr, nullptr, 1000) == NDIlib_frame_type_video) {
				// We can send it async, allowing us to capture right away
				::NDIlib_send_send_video_v2(pNDI_send, &video_frame);
				::NDIlib_recv_free_video_v2(pNDI_recv_lowQ, &video_frame);
			}
		}
	};

	auto forward_audio_fcn = [=](bool& exit) {
		while (!exit) {
			// Get the video frame
			NDIlib_audio_frame_v2_t audio_frame;
			if (NDIlib_recv_capture_v2(pNDI_recv_highQ, nullptr, &audio_frame, nullptr, 1000) == NDIlib_frame_type_audio) {
				// We can send it async, allowing us to capture right away
				::NDIlib_send_send_audio_v2(pNDI_send, &audio_frame);
				::NDIlib_recv_free_audio_v2(pNDI_recv_highQ, &audio_frame);
			}
		}
	};

	auto forward_metadata_fcn = [=](bool& exit) {
		while (!exit) {
			// Get the video frame
			NDIlib_metadata_frame_t metadata_frame;
			if (NDIlib_recv_capture_v2(pNDI_recv_highQ, nullptr, nullptr, &metadata_frame, 1000) == NDIlib_frame_type_metadata) {
				// We can send it async, allowing us to capture right away
				::NDIlib_send_send_metadata(pNDI_send, &metadata_frame);
				::NDIlib_recv_free_metadata(pNDI_recv_highQ, &metadata_frame);
			}
		}
	};

	// Start threads
	bool exit = false;

	// Start threads to get the best performance 
	std::thread forward_video_highQ(forward_video_highQ_fcn, std::ref(exit));
	std::thread forward_video_lowQ(forward_video_lowQ_fcn, std::ref(exit));
	std::thread forward_audio(forward_audio_fcn, std::ref(exit));
	std::thread forward_metadata(forward_audio_fcn, std::ref(exit));

	// Lets sleep for a bit.
	std::this_thread::sleep_for(std::chrono::minutes(5));

	// Mark us ready to exit
	exit = true;

	// Wait for the threads to exit
	forward_video_highQ.join();
	forward_video_lowQ.join();
	forward_audio.join();
	forward_metadata.join();

	// Destroy the NDI sender
	NDIlib_send_destroy(pNDI_send);

	// Destroy the receiver
	NDIlib_recv_destroy(pNDI_recv_highQ);

	// Not required, but nice
	NDIlib_destroy();

	// Finished
	return 0;
}
