#pragma once

#include "input/api/ControllerProvider.h"

#ifdef HAS_GAMECUBE

// libusb types (libusb.h is only included by the .cpp, so users of this header don't need libusb's include path)
struct libusb_context;
struct libusb_device;
struct libusb_device_handle;

// Official Nintendo GameCube controller adapter for Wii U / Switch (WUP-028, 057e:0337)
// and compatible third party adapters in "Wii U" mode.
// On Windows the adapter needs the WinUSB driver (installed once with Zadig, same as for Dolphin).
class GameCubeControllerProvider : public ControllerProviderBase
{
public:
	constexpr static size_t kMaxAdapters = 4;
	constexpr static size_t kMaxIndex = 4; // ports per adapter

	GameCubeControllerProvider();
	~GameCubeControllerProvider() override;

	inline static InputAPI::Type kAPIType = InputAPI::GameCube;
	InputAPI::Type api() const override { return kAPIType; }

	std::vector<std::shared_ptr<ControllerBase>> get_controllers() override;

	// the adapter is only opened once something uses this API,
	// so it isn't taken away from other programs (or from SDL) when nobody asked for it
	void start();

	uint32 get_adapter_count() const;
	bool has_rumble_connected(uint32 adapter_index) const;
	bool is_connected(uint32 adapter_index) const;
	bool is_port_connected(uint32 adapter_index, uint32 index) const;

	void set_rumble_state(uint32 adapter_index, uint32 index, bool state);

	enum PortType : uint8
	{
		kPortNone = 0,
		kPortWired = 1,
		kPortWireless = 2,
	};

	struct GCState
	{
		bool valid = false;
		PortType type = kPortNone;
		uint16 button = 0; // bit 0-11: A B X Y Left Right Down Up Start Z R L

		// raw value minus the controller's rest position (origin); sticks reach roughly -100..100
		sint16 lstick_x = 0;
		sint16 lstick_y = 0;
		sint16 rstick_x = 0;
		sint16 rstick_y = 0;

		// raw value minus the trigger's rest position; roughly 0..220
		sint16 ltrigger = 0;
		sint16 rtrigger = 0;
	};
	GCState get_state(uint32 adapter_index, uint32 index);

private:
	libusb_context* m_context = nullptr;

	std::mutex m_start_mutex;
	std::atomic_bool m_running = false;
	std::thread m_reader_thread, m_writer_thread;

	void reader_thread();
	void writer_thread();

	void scan_for_adapters();
	bool open_adapter(libusb_device* device, size_t slot);
	void close_adapter(size_t slot);
	void parse_report(size_t slot, const uint8* data);

	std::mutex m_writer_mutex;
	std::condition_variable m_writer_cond;
	bool m_rumble_changed = false;

	struct Origin
	{
		uint8 lstick_x = 128, lstick_y = 128, rstick_x = 128, rstick_y = 128, ltrigger = 0, rtrigger = 0;
	};

	struct Adapter
	{
		std::atomic<libusb_device_handle*> m_device_handle{};
		std::mutex m_handle_mutex; // held while the writer thread uses the handle and while it's being closed
		uint8 m_bus = 0, m_address = 0;
		bool m_kernel_driver_detached = false;

		uint8 m_endpoint_reader = 0x81, m_endpoint_writer = 0x02;

		mutable std::mutex m_state_mutex;
		std::array<GCState, kMaxIndex> m_states{};
		std::array<Origin, kMaxIndex> m_origins{};
		bool m_rumble_connected = false;

		std::array<bool, kMaxIndex> rumble_states{};
	};
	std::array<Adapter, kMaxAdapters> m_adapters;
};

#endif
