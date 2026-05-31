#include <cstdio>
#include <chrono>
#include <cassert>
#include <memory>
#include <inttypes.h>
#include <Processing.NDI.Advanced.h>

#ifdef _WIN32
#ifdef _WIN64
#pragma comment(lib, "Processing.NDI.Lib.Advanced.x64.lib")
#else // _WIN64
#pragma comment(lib, "Processing.NDI.Lib.Advanced.x86.lib")
#endif // _WIN64
#endif // _WIN32

// CComPtr
#include <atlbase.h>

// D3D11
#include <d3d11.h>
#pragma comment(lib, "d3d11.lib")

// Media foundaction
#include <mfapi.h>
#include <mftransform.h>
#include <codecapi.h>
#include <Mferror.h>
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")

//***********************************************************************************************************
// As a note, this SDK example shows how to use MFT and DirectX11 with NDI to get hardware accelerated on GPU
// video decompression. This code is fully functional and works well, however if you simply require GPU
// acceleration of video that does not remain on the GPU then the internal SDK implementation is
// significantly superior to this example and will result in much higher performance and also support GPU
// specific optimizations, multi-GPU load balancing and more.
struct video_decoder {
	// Constructor
	video_decoder(const NDIlib_video_frame_v2_t& video_frame);
	~video_decoder(void);

	// Submit compressed video data
	bool submit_data(const NDIlib_video_frame_v2_t& video_frame);

	// Was there an error
	bool error(void) const;

private:
	// The handle to the D3D Device
	CComPtr<ID3D11Device> m_pID3D11Device;
	CComPtr<IMFTransform> m_pIMFTransform;

	// Was there an error instantiating the format
	bool m_error = true;

	// The current sample time
	int64_t m_sample_time = 0;
	GUID m_source_format = {};

	// This will pull a frame from the transform
	bool receive_frame(void);

	// Change the input and output types
	bool set_input_type(void);
	bool update_output_type(void);
};

video_decoder::video_decoder(const NDIlib_video_frame_v2_t& video_frame)
{
	// Get the GPU by enumerating it
	HRESULT hr;
	CComPtr<ID3D11DeviceContext> pID3D11DeviceContext;
	if (FAILED(hr = ::D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_VIDEO_SUPPORT, nullptr, 0,
		D3D11_SDK_VERSION, &m_pID3D11Device, nullptr, &pID3D11DeviceContext))) {
		assert(false);
		return;
	}

	// Start media foundation
	if (FAILED(hr = ::MFStartup(MF_VERSION))) {
		assert(false);
		return;
	}

	// Check for supported formats
	switch (video_frame.FourCC) {
		case NDIlib_compressed_FourCC_type_H264:
			m_source_format = MFVideoFormat_H264;
			break;

		case NDIlib_compressed_FourCC_type_HEVC:
			m_source_format = MFVideoFormat_H264;
			break;

		default:
			return;
	}

	// We are looking for a codec that can convert from NV12 into H.264 (i.e. a video compressor !)
	const MFT_REGISTER_TYPE_INFO input_info = {MFMediaType_Video, m_source_format};
	const MFT_REGISTER_TYPE_INFO output_info = {MFMediaType_Video, MFVideoFormat_NV12};

	// if we could not get the GUID then we are toast
	if (output_info.guidSubtype == GUID_NULL)
		return;

	// We are now going to look for allowed decoders to use
	UINT32 codec_count = 0;
	IMFActivate** ppActivate = NULL;
	if (FAILED(hr = ::MFTEnumEx(MFT_CATEGORY_VIDEO_DECODER, MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SORTANDFILTER, &input_info, &output_info, &ppActivate, &codec_count))) {
		assert(false);
		return;
	}

	// If we found no decoders then we are toast
	if (!codec_count) {
		assert(false);
		return;
	}

	// We need to create a device manager
	UINT ResetToken;
	CComPtr<IMFDXGIDeviceManager> pIMFDXGIDeviceManager;
	if (FAILED(hr = ::MFCreateDXGIDeviceManager(&ResetToken, &pIMFDXGIDeviceManager))) {
		assert(false);
		return;
	}

	// We now assign it to the Direct3D device
	if (FAILED(hr = pIMFDXGIDeviceManager->ResetDevice(m_pID3D11Device, ResetToken))) {
		assert(false);
		return;
	}

	// Cycle over the codecs found
	for (UINT32 codec_no = 0; codec_no < codec_count; codec_no++) {
		// Make sure a previous transform is not in existance
		m_pIMFTransform.Release();

		// Try to create the codec
		if (FAILED(hr = ppActivate[codec_no]->ActivateObject(IID_IMFTransform, (void**)&m_pIMFTransform)))
			continue;

		// Lets get the attrivute store on the filter
		CComPtr<IMFAttributes> pIMFAttributes;
		if (FAILED(hr = m_pIMFTransform->GetAttributes(&pIMFAttributes)))
			continue;

		// This codec must support DirectX 11 or it cannot be used in this screen mirroring code.
		UINT32 supports_D3D11 = 0;
		if ((FAILED(hr = pIMFAttributes->GetUINT32(MF_SA_D3D11_AWARE, &supports_D3D11))) || (!supports_D3D11)) {
			assert(false);
			continue;
		}

		// We need to attach to the MFT codec
		if (FAILED(hr = m_pIMFTransform->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER, (ULONG_PTR)pIMFDXGIDeviceManager.p))) {
			assert(false);
			continue;
		}

		// The H.264 decoder needs to be enabled, which is weird
		if (FAILED(hr = pIMFAttributes->SetUINT32(CODECAPI_AVDecVideoAcceleration_H264, TRUE))) {
			assert(false);
			continue;
		}

		// We ignore the result of this low latency mode is not available all all OSs
		// We fail if this is not enabled or latency could be a problem, which is less than
		// perfect for an NDI implementation.
		if (FAILED(hr = pIMFAttributes->SetUINT32(CODECAPI_AVLowLatencyMode, TRUE))) {
			assert(false);
		}

		// Allowing for a buffer in flight is very important for performance reasons especially when doing
		// UHD (otherwise we aren't able to decode in real time).
		const int kMaxOutputSamples = 0;

		// If present, indicates the minimum number of progressive samples that the MFT should allow to be
		// outstanding (i.e. provided to the pipeline via ProcessOutput()) at any given time. This value is
		// only applicable to MFTs that allocate output samples themselves and use a circular allocator.
		// Other MFTs can ignore this attribute.
		if (FAILED(hr = pIMFAttributes->SetUINT32(MF_SA_MINIMUM_OUTPUT_SAMPLE_COUNT_PROGRESSIVE, kMaxOutputSamples))) {
			assert(false);
		}

		// If present, indicates the minimum number of samples that the MFT should allow to be outstanding
		// (i.e. provided to the pipeline via ProcessOutput()) at any given time. This value is only
		// applicable to MFTs that allocate output samples themselves and use A circular allocator. Other
		// MFTs can ignore this attribute.
		if (FAILED(hr = pIMFAttributes->SetUINT32(MF_SA_MINIMUM_OUTPUT_SAMPLE_COUNT, kMaxOutputSamples))) {
			assert(false);
		}

		// Configure the input and output types
		if (!set_input_type()) {
			assert(false);
			continue;
		}

		// Configure the input and output types
		if (!update_output_type()) {
			assert(false);
			continue;
		}

		// Start the stream
		if (FAILED(hr = m_pIMFTransform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0))) {
			assert(false);
			continue;
		}

		// No error, we are done and have a codec setup and ready to go
		break;
	}

	// Clear out the enumerations, and free the memory
	for (UINT32 codec_no = 0; codec_no < codec_count; ppActivate[codec_no++]->Release()) {
	}

	::CoTaskMemFree(ppActivate);

	// There was no error
	m_error = false;
}

video_decoder::~video_decoder(void)
{
	// We now ask the filter to drain all output
	if (!m_pIMFTransform)
		return;

	// Make sure that the transform is drained
	HRESULT hr;
	if (FAILED(hr = m_pIMFTransform->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0))) {
		assert(false);
	}

	// We make sure all samples are drained
	if (!receive_frame()) {
		assert(false);
	}
}

// Was there an error
bool video_decoder::error(void) const
{
	return m_error;
}

// This will pull a frame from the transform
bool video_decoder::receive_frame(void)
{
	// While there are going to pump all the samples from the transform that we can
	while (true) {
		// Lets see if we can get any decoded data
		MFT_OUTPUT_DATA_BUFFER OutputSamples = {0};
		DWORD status = 0;
		HRESULT hr = m_pIMFTransform->ProcessOutput(0, 1, &OutputSamples, &status);

		// Release any events, we couldn't care less about them
		CComPtr<IMFCollection> pIMFCollection;
		pIMFCollection.Attach(OutputSamples.pEvents);

		// Get the sample
		CComPtr<IMFSample> pIMFSample;
		pIMFSample.Attach(OutputSamples.pSample);

		// If I succeeded then I break out of the loop and return the decoded frame
		if (SUCCEEDED(hr)) {
			// Get the sample PTS
			int64_t sample_time;
			if (FAILED(pIMFSample->GetSampleTime(&sample_time))) {
				assert(false); continue;
			}

			// Ok then, we get the DirectX texture
			CComPtr<IMFMediaBuffer> pIMFMediaBuffer;
			if ((FAILED(hr = pIMFSample->GetBufferByIndex(0, &pIMFMediaBuffer))) || (!pIMFMediaBuffer)) {
				assert(false);
				continue;
			}

			// Debugging
			printf("ProcessOutput with time = %" PRId64 "\n", sample_time);

			// We are done
			continue;
		}

		// We have recovered all fo the data from the filter from the filter and so we are done now.
		if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT)
			return true;

		// These are the only things we really expect to bring us to this point in life.
		if (((hr == MF_E_TRANSFORM_STREAM_CHANGE) || (status & MFT_PROCESS_OUTPUT_STATUS_NEW_STREAMS) ||
			(OutputSamples.dwStatus & MFT_OUTPUT_DATA_BUFFER_FORMAT_CHANGE)) &&
			// Update the format being used.
			(update_output_type())) {
			continue;
		}

		// This is an actual error, which is not know quite how to support
		assert(false);
		return false;
	}
}

// Change the input and output types
bool video_decoder::set_input_type(void)
{
	// Cycle over the list
	for (uint32_t input_format_idx = 0;; input_format_idx++) {
		// Get the output format
		CComPtr<IMFMediaType> pIMFMediaType;
		HRESULT hr;
		if ((FAILED(hr = m_pIMFTransform->GetInputAvailableType(0, input_format_idx, &pIMFMediaType))) || (!pIMFMediaType)) {
			assert(false);
			return false;
		}

		// Get the output media type, it needs to be video or WTF ?
		GUID output_format_type = {0};
		if ((FAILED(hr = pIMFMediaType->GetGUID(MF_MT_MAJOR_TYPE, &output_format_type))) || (output_format_type != MFMediaType_Video)) {
			assert(false);
			continue;
		}

		// Lets get the media sub-type. Note that we only bother supporting NV12 which is what all current GPUs use.
		// There is no need to support other formats and those cannot even be tested paths.
		GUID subtype_check = GUID_NULL;
		if ((FAILED(hr = pIMFMediaType->GetGUID(MF_MT_SUBTYPE, &subtype_check))) || (subtype_check != m_source_format))
			continue;

		// We set this as the input format now. Whatever settings the filter wants can be used
		if (FAILED(hr = m_pIMFTransform->SetInputType(0, pIMFMediaType, 0))) {
			assert(false);
			continue;
		}

		// Configured
		return true;
	}
}

bool video_decoder::update_output_type(void)
{
	// Cycle over the list
	for (uint32_t output_format_idx = 0;; output_format_idx++) {
		// Get the output format
		CComPtr<IMFMediaType> pIMFMediaType;
		HRESULT hr;
		if (SUCCEEDED(hr = m_pIMFTransform->GetOutputAvailableType(0, output_format_idx, &pIMFMediaType))) {
			// Get the output media type, it needs to be video or WTF ?
			GUID format_type = GUID_NULL;
			if ((FAILED(hr = pIMFMediaType->GetGUID(MF_MT_MAJOR_TYPE, &format_type))) || (format_type != MFMediaType_Video)) {
				assert(false);
				continue;
			}

			// Lets get the media sub-type. Note that we only bother supporting NV12 which is what all current GPUs use.
			// There is no need to support other formats and those cannot even be tested paths.
			GUID subtype_check = GUID_NULL;
			if ((FAILED(hr = pIMFMediaType->GetGUID(MF_MT_SUBTYPE, &subtype_check))) || (subtype_check != MFVideoFormat_NV12))
				continue;
		} else {
			// We cannot encode this crap
			assert(false);
			return false;
		}

		// We now configure the output of this graph and hope that it works :)
		if (FAILED(hr = m_pIMFTransform->SetOutputType(0, pIMFMediaType, 0))) {
			assert(false);
			continue;
		}

		// Get the output properties, we need to make sure that it can provide buffers, which is what we want for D3D11 support.
		MFT_OUTPUT_STREAM_INFO stream_info = {};
		if ((FAILED(hr = m_pIMFTransform->GetOutputStreamInfo(0, &stream_info))) ||
			((stream_info.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES | MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES)) == 0)) {
			assert(false);
			continue;
		}

		// Set the video frame-rate
		uint32_t frame_rate_n, frame_rate_d;
		if (FAILED(hr = ::MFGetAttributeRatio(pIMFMediaType, MF_MT_FRAME_RATE, &frame_rate_n, &frame_rate_d))) {
			assert(false);
			continue;
		}

		// Set the video resolution
		uint32_t xres, yres;
		if (FAILED(hr = ::MFGetAttributeRatio(pIMFMediaType, MF_MT_FRAME_SIZE, &xres, &yres))) {
			assert(false);
			continue;
		}

		// Now set the attributes for the pixel aspect
		uint32_t pixel_aspect_ratio_n, pixel_aspect_ratio_d;
		if (FAILED(hr = ::MFGetAttributeRatio(pIMFMediaType, MF_MT_PIXEL_ASPECT_RATIO, &pixel_aspect_ratio_n, &pixel_aspect_ratio_d))) {
			assert(false);
			continue;
		}

		// Convert this to an aspect ratio
		printf("Format xres = %d\n", xres);
		printf("Format xres = %d\n", yres);
		printf("Format frame_rate = %d/%d\n", frame_rate_n, frame_rate_d);
		printf("Format pixel aspect_ratio = %d:%d\n", pixel_aspect_ratio_n, pixel_aspect_ratio_d);

		// Success
		return true;
	}
}

// Submit compressed video data
bool video_decoder::submit_data(const NDIlib_video_frame_v2_t& video_frame)
{
	// Get the compressed frame data
	const NDIlib_compressed_packet_t* p_packet = (const NDIlib_compressed_packet_t*)video_frame.p_data;
	const uint8_t* p_data = (uint8_t*)p_packet + p_packet->version;
	const uint32_t data_size = p_packet->data_size;

	// Create the sample to contain the bitstream packet
	HRESULT hr;
	CComPtr<IMFSample> pIMFSample;
	if ((FAILED(hr = MFCreateSample(&pIMFSample))) || (!pIMFSample)) {
		assert(false);
		return false;
	}

	// Allocate a media buffer
	CComPtr<IMFMediaBuffer> pIMFMediaBuffer;
	if ((FAILED(hr = MFCreateMemoryBuffer(data_size, &pIMFMediaBuffer))) || (!pIMFMediaBuffer)) {
		assert(false);
		return false;
	}

	// Set the buffer data
	BYTE* pBuffer;
	DWORD MaxLength, CurrentLength;
	if (FAILED(hr = pIMFMediaBuffer->Lock(&pBuffer, &MaxLength, &CurrentLength))) {
		assert(false);
		return false;
	}

	// Copy the data
	::memcpy(pBuffer, p_data, data_size);

	// We can unlock tbe buffer now
	if (FAILED(hr = pIMFMediaBuffer->Unlock())) {
		assert(false);
		return false;
	}

	// Set the stream data size
	if (FAILED(hr = pIMFMediaBuffer->SetCurrentLength(data_size))) {
		assert(false);
		return false;
	}

	// Add this buffer into the output
	if (FAILED(hr = pIMFSample->AddBuffer(pIMFMediaBuffer))) {
		assert(false);
		return false;
	}

	// Put the start time into the buffer
	if (FAILED(hr = pIMFSample->SetSampleTime(m_sample_time))) {
		assert(false);
		return false;
	}

	// Get the sample duration
	const int64_t duration = (10000000LL * video_frame.frame_rate_N) / video_frame.frame_rate_D;

	// Put in the duration
	if (FAILED(hr = pIMFSample->SetSampleDuration(duration))) {
		assert(false);
		return false;
	}

	// Debugging
	printf("ProcessInput with time = %" PRId64 "\n", m_sample_time);

	// Increment the time
	m_sample_time += duration;

	// If we cannot add more data yet, then we do not and we wait until at least one more frame has been decoded.
	if (FAILED(hr = m_pIMFTransform->ProcessInput(0, pIMFSample, 0))) {
		assert(false);
		return false;
	}

	// We pump any data from the queue at once here.
	if (!receive_frame()) {
		assert(false);
		return false;
	}

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

	// Connection.
	printf("Connecting to %s.\n", p_sources[0].p_ndi_name);

	// The create struct
	NDIlib_recv_create_v3_t NDI_recv_create;
	NDI_recv_create.source_to_connect_to = p_sources[0];
	NDI_recv_create.color_format = (NDIlib_recv_color_format_e)NDIlib_recv_color_format_ex_compressed_v4_with_audio;

	// We now have at least one source, so we create a receiver to look at it.
	NDIlib_recv_instance_t pNDI_recv = NDIlib_recv_create_v3(&NDI_recv_create);
	if (!pNDI_recv)
		return 0;

	// Destroy the NDI finder. We needed to have access to the pointers to p_sources[0]
	NDIlib_find_destroy(pNDI_find);

	// The current used FourCC for the codec
	NDIlib_FourCC_video_type_e codec_fourcc = (NDIlib_FourCC_video_type_e)0;
	std::shared_ptr<video_decoder> p_video_decoder;

	// Run for one minute
	using namespace std::chrono;
	for (const auto start = high_resolution_clock::now(); high_resolution_clock::now() - start < minutes(5);) {
		// The descriptors
		NDIlib_video_frame_v2_t video_frame;
		if (NDIlib_recv_capture_v2(pNDI_recv, &video_frame, nullptr, nullptr, 5000) == NDIlib_frame_type_video) {
			// Has the codec changed
			if (codec_fourcc != video_frame.FourCC) {
				// We allocate the new codec
				codec_fourcc = video_frame.FourCC;
				p_video_decoder = std::make_shared<video_decoder>(video_frame);

				// If we could not instantiate it for this codec then we cannot use it
				if (p_video_decoder->error())
					p_video_decoder.reset();
			}

			// If there is a video decoder then we decode it
			if (p_video_decoder) {
				// Decode the video
				p_video_decoder->submit_data(video_frame);
			} else {
				// This is not a supported codec.
				printf("Frame received with no decoder.\n");
			}

			// Free the video data
			NDIlib_recv_free_video_v2(pNDI_recv, &video_frame);
		}
	}

	// Destroy the receiver
	NDIlib_recv_destroy(pNDI_recv);

	// Not required, but nice
	NDIlib_destroy();

	// Finished
	return 0;
}
