#include "input/api/GameCube/GameCubeControllerProvider.h"
#include "input/api/GameCube/GameCubeController.h"

#ifdef HAS_GAMECUBE

#if BOOST_OS_BSD
#include <libusb.h>
#else
#include <libusb-1.0/libusb.h>
#endif

#ifdef interface
#undef interface
#endif

constexpr uint16_t kVendorId = 0x057e, kProductId = 0x0337;

// adapter protocol
constexpr uint8 kCmdStartPolling = 0x13;
constexpr uint8 kCmdRumble = 0x11;
constexpr uint8 kReportInput = 0x21;
constexpr size_t kInputReportSize = 37; // 0x21 + 4 * 9 bytes

GameCubeControllerProvider::GameCubeControllerProvider()
{
	const int ret = libusb_init(&m_context);
	if (ret != 0)
		throw std::runtime_error(fmt::format("GameCube adapter: libusb_init failed: {}", libusb_error_name(ret)));
}

GameCubeControllerProvider::~GameCubeControllerProvider()
{
	if (m_running)
	{
		m_running = false;
		m_writer_cond.notify_all();
		if (m_writer_thread.joinable())
			m_writer_thread.join();
		if (m_reader_thread.joinable())
			m_reader_thread.join();
	}

	for (size_t i = 0; i < kMaxAdapters; ++i)
		close_adapter(i);

	if (m_context)
	{
		libusb_exit(m_context);
		m_context = nullptr;
	}
}

void GameCubeControllerProvider::start()
{
	std::scoped_lock lock(m_start_mutex);
	if (m_running)
		return;

	scan_for_adapters();

	m_running = true;
	m_reader_thread = std::thread(&GameCubeControllerProvider::reader_thread, this);
	m_writer_thread = std::thread(&GameCubeControllerProvider::writer_thread, this);
}

std::vector<ControllerPtr> GameCubeControllerProvider::get_controllers()
{
	start();

	std::vector<ControllerPtr> result;
	// list all four ports of every connected adapter, plus the ports of adapter 1 so they can be assigned before plugging it in
	const auto adapter_count = std::max<uint32>(1, get_adapter_count());
	for (uint32 adapter_index = 0; adapter_index < adapter_count && adapter_index < kMaxAdapters; ++adapter_index)
	{
		for (uint32 index = 0; index < kMaxIndex; ++index)
			result.emplace_back(std::make_shared<GameCubeController>(adapter_index, index));
	}

	return result;
}

uint32 GameCubeControllerProvider::get_adapter_count() const
{
	uint32 count = 0;
	for (const auto& adapter : m_adapters)
	{
		if (adapter.m_device_handle)
			++count;
	}
	return count;
}

bool GameCubeControllerProvider::has_rumble_connected(uint32 adapter_index) const
{
	if (adapter_index >= m_adapters.size())
		return false;

	std::scoped_lock lock(m_adapters[adapter_index].m_state_mutex);
	return m_adapters[adapter_index].m_rumble_connected;
}

bool GameCubeControllerProvider::is_connected(uint32 adapter_index) const
{
	if (adapter_index >= m_adapters.size())
		return false;

	return m_adapters[adapter_index].m_device_handle != nullptr;
}

bool GameCubeControllerProvider::is_port_connected(uint32 adapter_index, uint32 index) const
{
	if (adapter_index >= m_adapters.size() || index >= kMaxIndex)
		return false;

	const auto& adapter = m_adapters[adapter_index];
	if (!adapter.m_device_handle)
		return false;

	std::scoped_lock lock(adapter.m_state_mutex);
	return adapter.m_states[index].valid && adapter.m_states[index].type != kPortNone;
}

void GameCubeControllerProvider::set_rumble_state(uint32 adapter_index, uint32 index, bool state)
{
	if (adapter_index >= m_adapters.size() || index >= kMaxIndex)
		return;

	std::scoped_lock lock(m_writer_mutex);
	m_adapters[adapter_index].rumble_states[index] = state;
	m_rumble_changed = true;
	m_writer_cond.notify_all();
}

GameCubeControllerProvider::GCState GameCubeControllerProvider::get_state(uint32 adapter_index, uint32 index)
{
	if (adapter_index >= m_adapters.size() || index >= kMaxIndex)
		return {};

	std::scoped_lock lock(m_adapters[adapter_index].m_state_mutex);
	return m_adapters[adapter_index].m_states[index];
}

void GameCubeControllerProvider::scan_for_adapters()
{
	libusb_device** devices;
	const auto count = libusb_get_device_list(m_context, &devices);
	if (count < 0)
	{
		cemuLog_log(LogType::Force, "GameCube adapter: libusb_get_device_list failed: {}", libusb_error_name(static_cast<int>(count)));
		return;
	}

	for (ssize_t i = 0; i < count; ++i)
	{
		libusb_device* device = devices[i];
		if (!device)
			continue;

		libusb_device_descriptor desc;
		if (libusb_get_device_descriptor(device, &desc) != 0)
			continue;
		if (desc.idVendor != kVendorId || desc.idProduct != kProductId)
			continue;

		const uint8 bus = libusb_get_bus_number(device);
		const uint8 address = libusb_get_device_address(device);

		// already open?
		bool known = false;
		for (const auto& adapter : m_adapters)
		{
			if (adapter.m_device_handle && adapter.m_bus == bus && adapter.m_address == address)
			{
				known = true;
				break;
			}
		}
		if (known)
			continue;

		for (size_t slot = 0; slot < kMaxAdapters; ++slot)
		{
			if (m_adapters[slot].m_device_handle)
				continue;

			open_adapter(device, slot);
			break;
		}
	}

	libusb_free_device_list(devices, 1);
}

bool GameCubeControllerProvider::open_adapter(libusb_device* device, size_t slot)
{
	auto& adapter = m_adapters[slot];

	libusb_device_handle* handle = nullptr;
	int ret = libusb_open(device, &handle);
	if (ret != 0)
	{
#if BOOST_OS_WINDOWS
		cemuLog_log(LogType::Force, "GameCube adapter: can't open the adapter ({}). Install the WinUSB driver for it with Zadig (\"WUP-028\"), the same way as for Dolphin.", libusb_error_name(ret));
#else
		cemuLog_log(LogType::Force, "GameCube adapter: can't open the adapter ({}). Check the USB device permissions (udev rule for 057e:0337).", libusb_error_name(ret));
#endif
		return false;
	}

	adapter.m_kernel_driver_detached = false;
	if (libusb_kernel_driver_active(handle, 0) == 1)
	{
		ret = libusb_detach_kernel_driver(handle, 0);
		if (ret != 0 && ret != LIBUSB_ERROR_NOT_SUPPORTED)
		{
			cemuLog_log(LogType::Force, "GameCube adapter: libusb_detach_kernel_driver failed: {}", libusb_error_name(ret));
			libusb_close(handle);
			return false;
		}
		adapter.m_kernel_driver_detached = ret == 0;
	}

	ret = libusb_claim_interface(handle, 0);
	if (ret != 0)
	{
		cemuLog_log(LogType::Force, "GameCube adapter: libusb_claim_interface failed: {}", libusb_error_name(ret));
		if (adapter.m_kernel_driver_detached)
			libusb_attach_kernel_driver(handle, 0);
		libusb_close(handle);
		return false;
	}

	// find the interrupt endpoints
	adapter.m_endpoint_reader = 0x81;
	adapter.m_endpoint_writer = 0x02;
	libusb_config_descriptor* config = nullptr;
	if (libusb_get_config_descriptor(device, 0, &config) == 0 && config)
	{
		for (int ic = 0; ic < config->bNumInterfaces; ic++)
		{
			const auto& interface = config->interface[ic];
			for (int j = 0; j < interface.num_altsetting; j++)
			{
				const auto& interface_desc = interface.altsetting[j];
				for (int k = 0; k < interface_desc.bNumEndpoints; k++)
				{
					const auto& endpoint = interface_desc.endpoint[k];
					if (endpoint.bEndpointAddress & LIBUSB_ENDPOINT_IN)
						adapter.m_endpoint_reader = endpoint.bEndpointAddress;
					else
						adapter.m_endpoint_writer = endpoint.bEndpointAddress;
				}
			}
		}
		libusb_free_config_descriptor(config);
	}

	// start polling: the adapter only sends input reports after this command
	int written = 0;
	uint8 payload = kCmdStartPolling;
	ret = libusb_interrupt_transfer(handle, adapter.m_endpoint_writer, &payload, sizeof(payload), &written, 100);
	if (ret != 0)
		cemuLog_log(LogType::Force, "GameCube adapter: start command failed: {}", libusb_error_name(ret));

	{
		std::scoped_lock lock(adapter.m_state_mutex);
		adapter.m_states = {};
		adapter.m_origins = {};
		adapter.m_rumble_connected = false;
	}
	{
		std::scoped_lock lock(m_writer_mutex);
		adapter.rumble_states = {};
	}

	adapter.m_bus = libusb_get_bus_number(device);
	adapter.m_address = libusb_get_device_address(device);
	adapter.m_device_handle = handle;

	cemuLog_log(LogType::Force, "GameCube adapter {} connected", slot + 1);
	return true;
}

void GameCubeControllerProvider::close_adapter(size_t slot)
{
	auto& adapter = m_adapters[slot];
	std::scoped_lock handle_lock(adapter.m_handle_mutex);
	if (const auto handle = adapter.m_device_handle.exchange(nullptr))
	{
		libusb_release_interface(handle, 0);
		if (adapter.m_kernel_driver_detached)
			libusb_attach_kernel_driver(handle, 0);
		libusb_close(handle);
		adapter.m_kernel_driver_detached = false;

		std::scoped_lock lock(adapter.m_state_mutex);
		adapter.m_states = {};
		adapter.m_rumble_connected = false;
		cemuLog_log(LogType::Force, "GameCube adapter {} disconnected", slot + 1);
	}
}

void GameCubeControllerProvider::parse_report(size_t slot, const uint8* data)
{
	auto& adapter = m_adapters[slot];
	std::scoped_lock lock(adapter.m_state_mutex);
	adapter.m_rumble_connected = false;
	for (size_t i = 0; i < kMaxIndex; ++i)
	{
		const uint8* port = data + 1 + i * 9;
		/*
		port[0]: status
			0x10 wired controller
			0x20 wireless controller (WaveBird)
			0x04 rumble power (grey USB plug) connected
		port[1]: A B X Y Left Right Down Up
		port[2]: Start Z R L
		port[3..8]: stick x, stick y, c-stick x, c-stick y, trigger L, trigger R
		*/
		const uint8 status = port[0];
		if (status & 0x04)
			adapter.m_rumble_connected = true;

		PortType type = kPortNone;
		if (status & 0x20)
			type = kPortWireless;
		else if (status & 0x10)
			type = kPortWired;

		auto& state = adapter.m_states[i];
		auto& origin = adapter.m_origins[i];
		if (type == kPortNone)
		{
			state = {};
			state.valid = true;
			continue;
		}

		// a controller reports its rest position (origin) right after it was plugged in
		if (state.type == kPortNone)
			origin = {port[3], port[4], port[5], port[6], port[7], port[8]};

		state.valid = true;
		state.type = type;
		state.button = (uint16)port[1] | ((uint16)(port[2] & 0x0F) << 8);
		state.lstick_x = (sint16)port[3] - origin.lstick_x;
		state.lstick_y = (sint16)port[4] - origin.lstick_y;
		state.rstick_x = (sint16)port[5] - origin.rstick_x;
		state.rstick_y = (sint16)port[6] - origin.rstick_y;
		state.ltrigger = std::max<sint16>(0, (sint16)port[7] - origin.ltrigger);
		state.rtrigger = std::max<sint16>(0, (sint16)port[8] - origin.rtrigger);
	}
}

void GameCubeControllerProvider::reader_thread()
{
	SetThreadName("GCAdapter::reader_thread");
	auto last_scan = std::chrono::steady_clock::now();
	while (m_running.load(std::memory_order_relaxed))
	{
		// look for newly plugged in adapters about once a second (libusb hotplug isn't available on Windows)
		const auto now = std::chrono::steady_clock::now();
		if (now - last_scan > std::chrono::seconds(1))
		{
			last_scan = now;
			if (get_adapter_count() < kMaxAdapters)
				scan_for_adapters();
		}

		bool any_adapter = false;
		for (size_t slot = 0; slot < kMaxAdapters; ++slot)
		{
			auto& adapter = m_adapters[slot];
			libusb_device_handle* handle = adapter.m_device_handle;
			if (!handle)
				continue;

			any_adapter = true;
			std::array<uint8, kInputReportSize> data{};
			int read = 0;
			const int result = libusb_interrupt_transfer(handle, adapter.m_endpoint_reader, data.data(), static_cast<int>(data.size()), &read, 16);
			if (result == 0)
			{
				if (read == (int)kInputReportSize && data[0] == kReportInput)
					parse_report(slot, data.data());
			}
			else if (result == LIBUSB_ERROR_NO_DEVICE || result == LIBUSB_ERROR_IO || result == LIBUSB_ERROR_PIPE)
			{
				cemuLog_log(LogType::Force, "GameCube adapter: read failed: {}", libusb_error_name(result));
				close_adapter(slot);
			}
			// LIBUSB_ERROR_TIMEOUT: no new report yet
		}

		if (!any_adapter)
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
}

void GameCubeControllerProvider::writer_thread()
{
	SetThreadName("GCAdapter::writer_thread");

	std::array<std::array<bool, kMaxIndex>, kMaxAdapters> sent_states{};

	std::unique_lock lock(m_writer_mutex);
	while (m_running.load(std::memory_order_relaxed))
	{
		m_writer_cond.wait_for(lock, std::chrono::milliseconds(250), [this] { return m_rumble_changed || !m_running; });
		if (!m_running)
			break;
		if (!m_rumble_changed)
			continue;
		m_rumble_changed = false;

		for (size_t i = 0; i < kMaxAdapters; ++i)
		{
			auto& adapter = m_adapters[i];
			libusb_device_handle* handle = adapter.m_device_handle;
			if (!handle)
				continue;

			if (adapter.rumble_states == sent_states[i])
				continue;

			sent_states[i] = adapter.rumble_states;
			std::array<uint8, 5> rumble{kCmdRumble, sent_states[i][0], sent_states[i][1], sent_states[i][2], sent_states[i][3]};

			lock.unlock();
			{
				std::scoped_lock handle_lock(adapter.m_handle_mutex);
				handle = adapter.m_device_handle;
				if (handle)
				{
					int written = 0;
					const int result = libusb_interrupt_transfer(handle, adapter.m_endpoint_writer, rumble.data(), static_cast<int>(rumble.size()), &written, 25);
					if (result != 0)
						cemuLog_log(LogType::Force, "GameCube adapter: rumble write failed: {}", libusb_error_name(result));
				}
			}
			lock.lock();
		}
	}
}

#endif
