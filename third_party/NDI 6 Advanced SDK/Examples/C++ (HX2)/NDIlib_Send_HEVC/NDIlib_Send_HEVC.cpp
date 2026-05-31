#include <csignal>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>
#include <Processing.NDI.Advanced.h>

// Basic video frame structure for the pre-compressed frames
struct video_frame {
	const uint8_t* p_data;
	const uint8_t* p_extra;
	int64_t  dts;
	int64_t  pts;
	uint32_t data_size;
	uint32_t extra_size;
	bool     is_keyframe;
};

// Basic set of data for a video frame to be sent via a scatter-gather list
struct video_send_data {
	NDIlib_compressed_packet_t packet;
	NDIlib_video_frame_v2_t    frame;
	std::vector<uint8_t*>      scatter_data;
	std::vector<int>           scatter_data_size;
};

// Basic audio frame structure for the pre-compressed AAC frames
struct audio_frame {
	const uint8_t* p_data;
	const uint8_t* p_extra;
	int64_t  dts;
	int64_t  pts;
	uint32_t data_size;
	uint32_t extra_size;
};

// Include the full resolution test data
namespace highQ {
#include "hevc_highQ.h"
}

// Include the preview resolution test data
namespace lowQ {
#include "hevc_lowQ.h"
}

// Include the AAC test data
namespace aac {
#include "aac.h"
}

struct stream_info_type {
	int xres, yres;
	int frame_rate_n, frame_rate_d;
	int aspect_ratio_n, aspect_ratio_d;
	int num_frames;
	const video_frame* p_frames;
};

static const stream_info_type video_lowQ_info = {
	lowQ::video_xres, lowQ::video_yres,
	lowQ::video_frame_rate_n, lowQ::video_frame_rate_d,
	lowQ::video_aspect_ratio_n, lowQ::video_aspect_ratio_d,
	lowQ::num_video_frames,
	lowQ::video_frames
};

static const stream_info_type video_highQ_info = {
	highQ::video_xres, highQ::video_yres,
	highQ::video_frame_rate_n, highQ::video_frame_rate_d,
	highQ::video_aspect_ratio_n, highQ::video_aspect_ratio_d,
	highQ::num_video_frames,
	highQ::video_frames
};

static std::atomic<bool> exit_loop(false);
static void sigint_handler(int)
{
	exit_loop = true;
}

template <typename FrameType>
uint32_t get_max_frame_size(const FrameType* p_frames, int num_frames)
{
	uint32_t max_frame_size = 0;

	// Iterate to find the largest frame size
	for (int i = 0; i != num_frames; i++)
		max_frame_size = std::max(max_frame_size, p_frames[i].data_size + p_frames[i].extra_size);

	return max_frame_size;
}

void init_video_data(video_send_data* p_send_data, int num_frames, const stream_info_type& stream_info, NDIlib_FourCC_video_type_ex_e stream_type)
{
	for (int i = 0; i != num_frames; i++) {
		NDIlib_video_frame_v2_t& frame = p_send_data[i].frame;

		// The following data is pretty much constant throughout the stream
		frame.FourCC = (NDIlib_FourCC_video_type_e)stream_type;
		frame.xres = stream_info.xres;
		frame.yres = stream_info.yres;
		frame.p_data = nullptr;
		frame.data_size_in_bytes = 0;
		frame.frame_rate_N = stream_info.frame_rate_n;
		frame.frame_rate_D = stream_info.frame_rate_d;
		frame.frame_format_type = NDIlib_frame_format_type_progressive;
		frame.picture_aspect_ratio = (float)stream_info.aspect_ratio_n / (float)stream_info.aspect_ratio_d;
	}
}

void setup_frame(int frame_num, const stream_info_type& stream_info, video_send_data& dst_data)
{
	const video_frame& src_frame = stream_info.p_frames[frame_num];

	// You should make sure that your timecode value is something "correct". Our display time-stamp is in the
	// 100ns format already so we can just use this as the timecode.
	dst_data.frame.timecode = src_frame.dts;

	// The compressed frame buffer needs to begin with the NDIlib_compressed_packet_t header. Note that we do
	// support arbitrary PTS and DTS values and we support full IBP frame sequences, we do not use the values
	// for anything other than correctly re-ordering frames so that they can be decoded and displayed
	// correctly; meaning that the time-base is not important. With all this said, it is somewhat our
	// recommendation to not use B frames since these increase the latency. Theoretically we would support
	// GOPs of almost any length although I-frames must be inserted when the SDK asks you too.
	dst_data.packet.version = sizeof(NDIlib_compressed_packet_t);
	dst_data.packet.pts = src_frame.pts;
	dst_data.packet.dts = src_frame.dts;
	dst_data.packet.flags = src_frame.is_keyframe ? NDIlib_compressed_packet_t::flags_keyframe : NDIlib_compressed_packet_t::flags_none;
	dst_data.packet.data_size = src_frame.data_size;
	dst_data.packet.extra_data_size = src_frame.extra_size;
	dst_data.packet.fourCC = NDIlib_compressed_FourCC_type_HEVC;

	// Ensure the scatter-gather lists are cleared for the sending of the next frame
	dst_data.scatter_data.clear();
	dst_data.scatter_data_size.clear();

	// Push the compressed packet structure to the scatter-gather lists
	dst_data.scatter_data.push_back((uint8_t*)&dst_data.packet);
	dst_data.scatter_data_size.push_back((int)sizeof(NDIlib_compressed_packet_t));

	// Push the actual frame data to the scatter-gather lists
	dst_data.scatter_data.push_back((uint8_t*)src_frame.p_data);
	dst_data.scatter_data_size.push_back((int)src_frame.data_size);

	// Push any required extra data to the scatter-gather lists
	if (src_frame.extra_size != 0) {
		dst_data.scatter_data.push_back((uint8_t*)src_frame.p_extra);
		dst_data.scatter_data_size.push_back((int)src_frame.extra_size);
	}

	// NULL terminate both lists
	dst_data.scatter_data.push_back(nullptr);
	dst_data.scatter_data_size.push_back(0);
}

void send_aac_audio(NDIlib_send_instance_t pNDI_send)
{
	// How many frames do we have total?
	const int num_frames = aac::num_audio_frames;

	// Initialize the packet structure which would be the header data for each audio frame
	NDIlib_compressed_packet_t packet = {};
	packet.version = sizeof(NDIlib_compressed_packet_t);
	packet.fourCC = NDIlib_compressed_FourCC_type_AAC;
	packet.flags = NDIlib_compressed_packet_t::flags_keyframe;

	// The following data is pretty much constant throughout the stream. Other fields will be calculated on
	// a per-frame basis.
	NDIlib_audio_frame_v3_t dst_frame = {};
	dst_frame.sample_rate = aac::audio_sample_rate;
	dst_frame.no_channels = aac::audio_num_channels;
	dst_frame.no_samples = aac::audio_num_samples;
	dst_frame.FourCC = (NDIlib_FourCC_audio_type_e)NDIlib_FourCC_audio_type_ex_AAC;

	std::vector<uint8_t*> scatter_data;
	std::vector<int>      scatter_data_size;

	// Keep sending frames until SIGINT arrives
	for (int frame_num = 0; !exit_loop; frame_num = (frame_num + 1) % num_frames) {
		const audio_frame& src_frame = aac::audio_frames[frame_num];

		// You should make sure that your timecode value is something "correct". Our display time-stamp is in
		// the 100ns format already so we can just use this as the timecode.
		dst_frame.timecode = src_frame.dts;

		// Initialize the non-const data of the packet header
		packet.pts = src_frame.pts;
		packet.dts = src_frame.dts;
		packet.data_size = src_frame.data_size;
		packet.extra_data_size = src_frame.extra_size;

		// Ensure the scatter-gather lists are cleared for the sending of the next frame
		scatter_data.clear();
		scatter_data_size.clear();

		// Push the compressed packet structure to the scatter-gather lists
		scatter_data.push_back((uint8_t*)&packet);
		scatter_data_size.push_back((int)sizeof(NDIlib_compressed_packet_t));

		// Push the actual frame data to the scatter-gather lists
		scatter_data.push_back((uint8_t*)src_frame.p_data);
		scatter_data_size.push_back((int)src_frame.data_size);

		// Push any required extra data to the scatter-gather lists
		if (src_frame.extra_size != 0) {
			scatter_data.push_back((uint8_t*)src_frame.p_extra);
			scatter_data_size.push_back((int)src_frame.extra_size);
		}

		// NULL terminate both lists
		scatter_data.push_back(nullptr);
		scatter_data_size.push_back(0);

		// These are the scatter-gather lists to use
		NDIlib_frame_scatter_t frame_scatter = {scatter_data.data(), scatter_data_size.data()};

		// Send the AAC audio frame
		NDIlib_send_send_audio_scatter(pNDI_send, &dst_frame, &frame_scatter);
	}
}

int main(int argc, char* argv[])
{
	// Catch interrupt so that we can shut down gracefully
	signal(SIGINT, sigint_handler);

	// Not required, but "correct" (see the SDK documentation).
	if (!NDIlib_initialize())
		return 0;

	// We create the NDI sender properties. Please note that for this example we are going to allow the SDK
	// to clock the sending and receiving. It is likely that if you have a device which has it's own video
	// clock that delivers video at the correct rate that you would specify these as false.
	NDIlib_send_create_t NDI_send_description;
	NDI_send_description.p_ndi_name = "HEVC HX Test";
	NDI_send_description.clock_video = true;
	NDI_send_description.clock_audio = true;

	// Now create the sender. Note that for normal implementations you would specify your configuration
	// settings in the separate parameters per the SDK documentation. Most likely these would at very least
	// include your vendor-id which is needed for product use of the NDI Advanced SDK. You can however test
	// without out. This configuration allows all NDI settings to be configured as needed (UDP mode, etc...)
	// although if it is not present then it falls back to the default parameters in a configuration file.
	NDIlib_send_instance_t pNDI_send = NDIlib_send_create_v2(&NDI_send_description, /* See note above */nullptr);
	if (!pNDI_send) return 0;

	// Begin sending AAC audio on its own thread
	std::thread audio_thread(send_aac_audio, pNDI_send);

	// How many frames do we have total? In this example, the low quality and high quality structures should
	// have the same number of frames.
	const int num_frames = video_highQ_info.num_frames;

	// Initialize some video frames for each stream for asynchronous sending
	video_send_data highQ_data[2], lowQ_data[2];
	init_video_data(highQ_data, 2, video_highQ_info, NDIlib_FourCC_video_type_ex_HEVC_highest_bandwidth);
	init_video_data(lowQ_data, 2, video_lowQ_info, NDIlib_FourCC_video_type_ex_HEVC_lowest_bandwidth);

	// Keep sending frames until SIGINT arrives
	for (int frame_num = 0, send_frame = 0; !exit_loop; frame_num = (frame_num + 1) % num_frames) {
		// In this test code, if a NDI sender instance needs a keyframe, we just go back to the animation. In
		// a real application this is NOT what you would do. Instead you will tell your compressor to
		// generate a new I-Frame as quickly as it can. You may insert I-Frames at regular intervals yourself
		// if you want, this function simply tells you when you MUST insert one very quickly (within <100ms).
		// This call will return true when there are new connections, there are connections that dropped
		// frames (e.g. we correctly detect UDP drops and request a new I-Frame to avoid having corrupted
		// video, etc...). We support any IBP frame sequences and support very long GOPs. Our general
		// recommendation however would be that it is best to use I and P frames to keep decode latency lower
		// (B frames add delay). You can write out multi-second (or even longer) GOPs if you want, allowing
		// the bandwidth to be very low indeed. NDI will request an I-Frame whenever it is truly needed for
		// the connection to correctly decode a stream.
		if (NDIlib_send_is_keyframe_required(pNDI_send)) {
			// Normally we would tell the compressor to insert a new I-Frame here. For our example we just
			// jump back to the beginning frame since we know this was an I-Frame. This is NOT what you would
			// do in production code, this is just an example that does not include support for a HW encoder.
			frame_num = 0;
		}

		// Prepare the next high quality frame to be sent out. This should be a full resolution frame at the
		// quality and format that your device supports and the user has selected. Dynamically changing video
		// formats are fully supported although you should of course insert an I-Frame when you change format.
		setup_frame(frame_num, video_highQ_info, highQ_data[send_frame]);
		NDIlib_video_frame_v2_t& highQ_frame = highQ_data[send_frame].frame;
		NDIlib_frame_scatter_t   highQ_scatter = {highQ_data[send_frame].scatter_data.data(), highQ_data[send_frame].scatter_data_size.data()};

		// Prepare the next low quality frame to be sent out. This MUST be a 640 wide preview resolution
		// stream. At 16:9 this would mean typically that the frame resolution is 640x360 if you assume
		// square pixels. We do not enforce that this is timed in sync with the high quality stream although
		// it is preferable. It is fine if you wish to run this stream at a lower frame rate (e.g. 30Hz
		// instead of 60Hz) although once again it is preferable if it is a full frame rate stream.
		setup_frame(frame_num, video_lowQ_info, lowQ_data[send_frame]);
		NDIlib_video_frame_v2_t& lowQ_frame = lowQ_data[send_frame].frame;
		NDIlib_frame_scatter_t   lowQ_scatter = {lowQ_data[send_frame].scatter_data.data(), lowQ_data[send_frame].scatter_data_size.data()};

		// Estimate the current bit rate of the frames. Note that because this example does not include a 
		// compressor we ignore it, however we keep this here since you most likely want to use it. This does
		// not take into account IBP frame sequences, etc... but instead gives you an average frame size that
		// respects what "typical NDI|HX devices use".
		const int highQ_frame_size = NDIlib_send_get_target_frame_size(pNDI_send, &highQ_frame);
		const int lowQ_frame_size = NDIlib_send_get_target_frame_size(pNDI_send, &lowQ_frame);

		// Send both frames asynchronously so that we can begin preparing the next frames. We are using
		// asynchronous sending here which is defined in the SDK documentation. A call to the SDK will return
		// immediately and the frame data passed into the function should not be changed until the next call
		// to send_video_async. In other words, when the function return from NDIlib_send_send_video_async_v2
		// then all buffers from previous calls are no longer in use by the SDK.
		NDIlib_send_send_video_scatter_async(pNDI_send, &highQ_frame, &highQ_scatter);
		NDIlib_send_send_video_scatter_async(pNDI_send, &lowQ_frame, &lowQ_scatter);

		// Advance the next frame buffer
		send_frame = (send_frame + 1) % 2;
	}

	// Ensure all async operations have completed, especially before releasing the streams' memory. Note that
	// this ensures that there are no buffers in flight in the SDK.
	NDIlib_send_send_video_scatter_async(pNDI_send, nullptr, nullptr);

	// Wait for the audio thread to complete
	audio_thread.join();

	// Destroy the NDI sender
	NDIlib_send_destroy(pNDI_send);

	// Not required, but nice
	NDIlib_destroy();

	// Finished
	return 0;
}
