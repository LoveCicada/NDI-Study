#include <csignal>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <atomic>
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
	// Not required, but "correct" (see the SDK documentation.
	if (!NDIlib_initialize()) {
		// Cannot run NDI. Most likely because the CPU is not sufficient (see SDK documentation).
		// you can check this directly with a call to NDIlib_is_supported_CPU()
		printf("Cannot run NDI.");
		return 0;
	}

	// Catch interrupt so that we can shut down gracefully
	signal(SIGINT, sigint_handler);

	// Create an NDI source that is called "My Video" and is clocked to the video.
	NDIlib_send_create_t NDI_send_create_desc;
	NDI_send_create_desc.p_ndi_name = "My Video";

	// We create the NDI sender
	NDIlib_send_instance_t pNDI_send = NDIlib_send_create(&NDI_send_create_desc);
	if (!pNDI_send)
		return 0;

	// We are going to create a 1920x1080 interlaced frame at 29.97Hz.
	NDIlib_video_frame_v2_t NDI_video_frame;
	NDI_video_frame.xres = 1920;
	NDI_video_frame.yres = 1080;
	NDI_video_frame.FourCC = NDIlib_FourCC_type_BGRA;
	NDI_video_frame.line_stride_in_bytes = 1920 * 4;

	// We are going to provide an asynchronous completion routine.
	NDIlib_send_set_video_async_completion(
		pNDI_send,
		nullptr,
		[](void* p_opaque, const NDIlib_video_frame_v2_t* p_video_data) {
			// We can just free the buffer
			delete[] p_video_data->p_data;
		}
	);

	// We will send 1000 frames of video. 
	for (int idx = 0; idx < 1000; idx++) {
		// We can allocate a new buffer here, knowing that a completion event will be called when it may be freed
		NDI_video_frame.p_data = new uint8_t[NDI_video_frame.line_stride_in_bytes * NDI_video_frame.yres];
		memset(NDI_video_frame.p_data, (idx & 1) ? 255 : 0, NDI_video_frame.line_stride_in_bytes * NDI_video_frame.yres);

		// We now submit the frame asynchronously. This means that this call will return immediately and when the frame sending is complete
		// the completion will be called.
		NDIlib_send_send_video_async_v2(pNDI_send, &NDI_video_frame);
	}

	// Destroy the NDI sender
	NDIlib_send_destroy(pNDI_send);

	// Not required, but nice
	NDIlib_destroy();

	// Success
	return 0;
}
