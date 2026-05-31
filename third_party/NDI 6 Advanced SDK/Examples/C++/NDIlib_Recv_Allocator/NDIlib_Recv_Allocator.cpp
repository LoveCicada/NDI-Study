#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <Processing.NDI.Advanced.h>

#ifdef _WIN32
#ifdef _WIN64
#pragma comment(lib, "Processing.NDI.Lib.Advanced.x64.lib")
#else // _WIN64
#pragma comment(lib, "Processing.NDI.Lib.Advanced.x86.lib")
#endif // _WIN64
#endif // _WIN32

// This shows how to process decompressed buffers
// We first have to check what format is wanted for decompression. Note that if you are using a known format then you do not need
// to support all of the possible color spaces. This is provided for reference.
bool video_custom_allocator(void* p_opaque, NDIlib_video_frame_v2_t* p_video_data)
{
	switch (p_video_data->FourCC) {
		case NDIlib_FourCC_video_type_UYVY:
			p_video_data->line_stride_in_bytes = p_video_data->xres * 2;
			p_video_data->p_data = (uint8_t*)::malloc(p_video_data->line_stride_in_bytes * p_video_data->yres);
			break;

		case NDIlib_FourCC_video_type_UYVA:
			p_video_data->line_stride_in_bytes = p_video_data->xres * 2;
			p_video_data->p_data = (uint8_t*)::malloc(p_video_data->line_stride_in_bytes * p_video_data->yres + /* Alpha */p_video_data->line_stride_in_bytes / 2 * p_video_data->yres);
			break;

		case NDIlib_FourCC_video_type_P216:
			p_video_data->line_stride_in_bytes = p_video_data->xres * 2 * sizeof(int16_t);
			p_video_data->p_data = (uint8_t*)::malloc(p_video_data->line_stride_in_bytes * p_video_data->yres);
			break;

		case NDIlib_FourCC_video_type_PA16:
			p_video_data->line_stride_in_bytes = p_video_data->xres * 2 * sizeof(int16_t);
			p_video_data->p_data = (uint8_t*)::malloc(p_video_data->line_stride_in_bytes * p_video_data->yres + /* Alpha */p_video_data->line_stride_in_bytes / 2 * p_video_data->yres);
			break;

		case NDIlib_FourCC_video_type_BGRA:
		case NDIlib_FourCC_video_type_BGRX:
		case NDIlib_FourCC_video_type_RGBA:
		case NDIlib_FourCC_video_type_RGBX:
			p_video_data->line_stride_in_bytes = p_video_data->xres * 4;
			p_video_data->p_data = (uint8_t*)::malloc(p_video_data->line_stride_in_bytes * p_video_data->yres);
			break;

		default:
			// Error, not a supported FourCC
			p_video_data->line_stride_in_bytes = 0;
			p_video_data->p_data = nullptr;
			return false;
	}

	// Success
	return true;
}

bool video_custom_deallocator(void* p_opaque, const NDIlib_video_frame_v2_t* p_video_data)
{
	// Free the memory
	::free(p_video_data->p_data);

	// Success
	return true;
}

bool audio_custom_allocator(void* p_opaque, NDIlib_audio_frame_v3_t* p_audio_data)
{
	// Allocate uncompressed audio
	switch (p_audio_data->FourCC) {
		case NDIlib_FourCC_audio_type_FLTP:
			p_audio_data->channel_stride_in_bytes = sizeof(float) * p_audio_data->no_samples;
			p_audio_data->p_data = (uint8_t*)::malloc(p_audio_data->channel_stride_in_bytes * p_audio_data->no_channels);
			break;

		default:
			p_audio_data->channel_stride_in_bytes = 0;
			p_audio_data->p_data = nullptr;
			return false;
	}

	// Success
	return true;
}

bool audio_custom_deallocator(void* p_opaque, const NDIlib_audio_frame_v3_t* p_audio_data)
{
	// Free the memory
	::free(p_audio_data->p_data);

	// Success
	return true;
}

int main(int argc, char* argv[])
{
	// Not required, but "correct" (see the SDK documentation.
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

	// We can set a custom memory allocator at any point. It is better to assign it
	// before the connection to avoid any chance of there being frames that we did 
	// not allocat with the custom allocator.
	NDIlib_recv_set_video_allocator(pNDI_recv, nullptr/* No per instance data needed */, video_custom_allocator, video_custom_deallocator);
	NDIlib_recv_set_audio_allocator(pNDI_recv, nullptr/* No per instance data needed */, audio_custom_allocator, audio_custom_deallocator);

	// Connect to our sources
	NDIlib_recv_connect(pNDI_recv, p_sources + 0);

	// Destroy the NDI finder. We needed to have access to the pointers to p_sources[0]
	NDIlib_find_destroy(pNDI_find);

	// Run for one minute
	using namespace std::chrono;
	for (const auto start = high_resolution_clock::now(); high_resolution_clock::now() - start < minutes(5);) {
		// The descriptors
		NDIlib_video_frame_v2_t video_frame;
		NDIlib_audio_frame_v2_t audio_frame;

		switch (NDIlib_recv_capture_v2(pNDI_recv, &video_frame, &audio_frame, nullptr, 5000)) {
			// No data
			case NDIlib_frame_type_none:
				printf("No data received.\n");
				break;

			// Video data
			case NDIlib_frame_type_video:
				printf("Video data received (%dx%d).\n", video_frame.xres, video_frame.yres);
				NDIlib_recv_free_video_v2(pNDI_recv, &video_frame);
				break;

			// Audio data
			case NDIlib_frame_type_audio:
				printf("Audio data received (%d samples).\n", audio_frame.no_samples);
				NDIlib_recv_free_audio_v2(pNDI_recv, &audio_frame);
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
