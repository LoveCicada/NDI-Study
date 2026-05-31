#include <csignal>
#include <cstdio>
#include <atomic>
#include <chrono>
#include <Processing.NDI.Advanced.h>

#ifdef _WIN32
#define strncasecmp _strnicmp

#ifdef _WIN64
#pragma comment(lib, "Processing.NDI.Lib.Advanced.x64.lib")
#else // _WIN64
#pragma comment(lib, "Processing.NDI.Lib.Advanced.x86.lib")
#endif // _WIN64
#else
#include <strings.h>
#endif

static std::atomic<bool> exit_loop(false);
static void sigint_handler(int)
{
	exit_loop = true;
}

int main(int argc, char* argv[])
{
	// Not required, but "correct" (see the SDK documentation).
	if (!NDIlib_initialize()) {
		// Cannot run NDI. Most likely because the CPU is not sufficient (see SDK documentation).
		// you can check this directly with a call to NDIlib_is_supported_CPU()
		printf("Cannot run NDI.");
		return 0;
	}

	// Catch interrupt so that we can shut down gracefully
	signal(SIGINT, sigint_handler);

	// Start by creating an NDI Genlock object
	NDIlib_source_t source_name("PROXY (NDIlib_Send_Video)");
	NDIlib_genlock_instance_t p_genlock = NDIlib_genlock_create(&source_name, nullptr/* Note that your vendor JSON is required here */);

	// Are we genlocked
	bool is_genlocked = false;

	// Run for one minute
	using namespace std::chrono;
	for (const auto start = high_resolution_clock::now(); !exit_loop && high_resolution_clock::now() - start < minutes(5);) {
		// Get the current time
		const auto start_send = high_resolution_clock::now();

		// Send 200 frames
		for (int idx = 200; idx; idx--) {
			// Setup the frame header.
			NDIlib_video_frame_v2_t frame;
			frame.frame_rate_N = 30000;
			frame.frame_rate_D = 1001;
			frame.frame_format_type = NDIlib_frame_format_type_progressive;

			// We now wait according to the genlock signal.
			const bool genlock_enabled = NDIlib_genlock_wait_video(p_genlock, &frame);

			// Here you could send your frame as needed. You want to only have one genlock source
			// in your application if possible and so you might need to trigger an event and do the
			// sending across many senders on another thread. It is also recommended that you use the
			// NDIlib_send_send_async functions since those return immediately and allow the genlock
			// to correctly pace you without any compression or network sending time interacting with
			// your main rendering loop.
			// 
			// Note that you NEED to create any NDI senders with:
			//		NDI_send_create_desc.clock_video = false;
			//		NDI_send_create_desc.clock_audio = false;
			// 
			// If you do not then the sender will try to use the system clock to pace your sending
			// which then defeats the purpose of genlock !

			// Display the status
			if (genlock_enabled != is_genlocked) {
				// Store the current state
				printf("Genlocked to %s.\n", genlock_enabled ? "Remote Source" : "System Clock");
				is_genlocked = genlock_enabled;
			}
		}

		// Just display something helpful
		printf("200 frames sent, at %1.2ffps\n", 200.0f / duration_cast<duration<float>>(high_resolution_clock::now() - start_send).count());
	}

	// Destroy the genlock object
	NDIlib_genlock_destroy(p_genlock);

	// Not required, but nice
	NDIlib_destroy();

	// Success
	return 0;
}

