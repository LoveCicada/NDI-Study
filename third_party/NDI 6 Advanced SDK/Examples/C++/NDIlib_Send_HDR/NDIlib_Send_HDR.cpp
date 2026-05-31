#include <cstdio>
#include <chrono>
#include <string>
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

	// Setup the NDI sender parameters
	NDIlib_send_create_t ndi_send_create_desc;
	ndi_send_create_desc.p_ndi_name = "Example Send HDR";

	// We create the NDI sender
	NDIlib_send_instance_t pNDI_send = NDIlib_send_create(&ndi_send_create_desc);
	if (!pNDI_send)
		return 0;

	// We are going to create a 1920x1080 frame in V210 (10 bit packed)
	NDIlib_video_frame_v2_t NDI_video_frame_16bit;
	NDI_video_frame_16bit.xres = 1920;
	NDI_video_frame_16bit.yres = 1080;
	NDI_video_frame_16bit.FourCC = NDIlib_FourCC_video_type_P216;
	NDI_video_frame_16bit.line_stride_in_bytes = NDI_video_frame_16bit.xres * sizeof(uint16_t);
	NDI_video_frame_16bit.p_data = (uint8_t*)malloc(NDI_video_frame_16bit.line_stride_in_bytes * 2 * NDI_video_frame_16bit.yres);

	// HDR require metadata information in video frame
	// note 'transfer' have two possible values for HDR: bt_2100_pq or bt_2100_hlg
	NDI_video_frame_16bit.p_metadata =
		"<ndi_color_info "
		"	transfer=\"bt_2100_hlg\" "
		"	matrix=\"bt_2100\" "
		"	primaries=\"bt_2100\" "
		"/> ";

	using namespace std::chrono;
	const auto start = high_resolution_clock::now();

	// Run for five minutes
	while (high_resolution_clock::now() - start < minutes(5)) {
		// Create a gray frame, changing the luminance creating a fading gray
		// Why is it useful in HDR? In HDR you don't see 'defined horizontal lines' on screen
		for (int y = 0; y < NDI_video_frame_16bit.yres; y++) {
			// Get the line pointer
			uint16_t* p_line_y = (uint16_t*)(NDI_video_frame_16bit.p_data + y * NDI_video_frame_16bit.line_stride_in_bytes);
			uint16_t* p_line_cbcr = (uint16_t*)((uint8_t*)p_line_y + NDI_video_frame_16bit.yres * NDI_video_frame_16bit.line_stride_in_bytes);

			// set luminance, starts 0 and go to max uint16
			for (size_t x = 0; x < NDI_video_frame_16bit.xres; x++) {
				p_line_y[x] = (uint16_t)x * (((float)std::numeric_limits<uint16_t>::max() / NDI_video_frame_16bit.xres));
			}

			// The chroma is all gray
			for (size_t x = 0; x < NDI_video_frame_16bit.xres; x += 2) {
				p_line_cbcr[x + 0] = std::numeric_limits<uint16_t>::max() / 2; // Cb
				p_line_cbcr[x + 1] = std::numeric_limits<uint16_t>::max() / 2; // Cr
			}
		}
		// We now submit the frame.
		NDIlib_send_send_video_v2(pNDI_send, &NDI_video_frame_16bit);
	}

	// Free the video frame
	free(NDI_video_frame_16bit.p_data);

	// Destroy the NDI sender
	NDIlib_send_destroy(pNDI_send);

	// Not required, but nice
	NDIlib_destroy();

	// Success
	return 0;
}
