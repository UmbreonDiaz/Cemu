#include "Cafe/OS/common/OSCommon.h"
#include "Cafe/HW/Espresso/PPCCallback.h"
#include "Cafe/OS/libs/nn_hpad/nn_hpad.h"
#include "Cafe/OS/libs/coreinit/coreinit_Time.h"
#include "Cafe/OS/libs/coreinit/coreinit_Alarm.h"
#include "input/InputManager.h"

// Reference: wut include/nn/hpad/hpad.h, and how Super Smash Bros. for Wii U uses the library
// (HPADInit, HPADSetGgggConnectCallback, HPADSetConnectCallback, HPADRead with 16 buffers, HPADControlMotor).
//
// Channels 0-3 are the four ports of adapter 0 (GGGG channel 0), channels 4-7 the ports of adapter 1.
// Channel N is the Nth emulated controller of type "GameCube Controller".

constinit COSStaticData s_hpadStaticData;

namespace nn::hpad
{
	constexpr sint32 kChannelCount = 8;
	constexpr sint32 kAdapterCount = 2;
	constexpr sint32 kPortsPerAdapter = 4;
	constexpr sint32 kSampleBufferCount = 16;

	// HPADStatus value ranges (same clamping as the GameCube's PADClamp)
	constexpr float kStickMax = 56.0f;
	constexpr float kSubstickMax = 44.0f;
	constexpr float kTriggerMax = 150.0f;

	enum HPADResult : sint32
	{
		HPAD_RESULT_OK = 0,
		HPAD_RESULT_NO_CONTROLLER = -2, // there is no controller on this channel
		HPAD_RESULT_INVALID = -6,
	};

	// connect callback status
	constexpr sint32 kStatusConnected = 0;
	constexpr sint32 kStatusDisconnected = -1;

	enum HPADControllerType : uint8
	{
		HPAD_CONTROLLER_NONE = 0,
		HPAD_CONTROLLER_WIRED = 1,
		HPAD_CONTROLLER_WIRELESS = 2,
	};

	enum HPADMotorCommand : uint32
	{
		HPAD_MOTOR_COMMAND_STOP = 0,
		HPAD_MOTOR_COMMAND_RUMBLE = 1,
		HPAD_MOTOR_COMMAND_STOP_HARD = 2,
	};

	enum HPADStickEmulation : uint32
	{
		HPAD_STICK_EMULATION_LEFT = 0x00001000,
		HPAD_STICK_EMULATION_RIGHT = 0x00002000,
		HPAD_STICK_EMULATION_DOWN = 0x00004000,
		HPAD_STICK_EMULATION_UP = 0x00008000,
		HPAD_SUBSTICK_EMULATION_LEFT = 0x00010000,
		HPAD_SUBSTICK_EMULATION_RIGHT = 0x00020000,
		HPAD_SUBSTICK_EMULATION_DOWN = 0x00040000,
		HPAD_SUBSTICK_EMULATION_UP = 0x00080000,
	};

	struct HPADStatus
	{
		sint32be hold;
		sint32be trigger;
		sint32be release;
		sint8 stickX;
		sint8 stickY;
		sint8 substickX;
		sint8 substickY;
		uint8 triggerL;
		uint8 triggerR;
		uint8 status; // HPADControllerType
		uint8 padding13;
		sint32be error;
	};
	static_assert(sizeof(HPADStatus) == 0x18);
	static_assert(offsetof(HPADStatus, stickX) == 0x0C);
	static_assert(offsetof(HPADStatus, triggerL) == 0x10);
	static_assert(offsetof(HPADStatus, status) == 0x12);
	static_assert(offsetof(HPADStatus, error) == 0x14);

	struct HPADGGGGStatus
	{
		uint32be connected;
		uint32be powerSupplyConnected;
		uint32be active;
	};
	static_assert(sizeof(HPADGGGGStatus) == 0xC);

	struct
	{
		CafeStaticVar<coreinit::OSAlarm_t> alarm{s_hpadStaticData};
		bool initialized = false;

		struct Channel
		{
			MEMPTR<void> connectCallback;
			bool reportedConnected = false;
			uint32 lastHold = 0;
			bool rumbling = false;
		} channel[kChannelCount];

		struct Adapter
		{
			MEMPTR<void> connectCallback;
			MEMPTR<void> powerSupplyCallback;
			MEMPTR<void> samplingCallback;
			MEMPTR<void> resetCallback;
			bool resetPending = false;
			bool reportedConnected = false;
			bool reportedPowerSupply = false;
		} adapter[kAdapterCount];
	} g_hpad;

	std::shared_ptr<GameCubePadController> GetController(sint32 chan)
	{
		if (chan < 0 || chan >= kChannelCount)
			return {};
		return InputManager::instance().get_hpad_controller(chan);
	}

	// the adapter counts as plugged in when at least one of its four ports has an emulated GameCube controller
	bool IsAdapterConnected(sint32 adapterIndex)
	{
		for (sint32 port = 0; port < kPortsPerAdapter; ++port)
		{
			if (GetController(adapterIndex * kPortsPerAdapter + port))
				return true;
		}
		return false;
	}

	sint8 ToStickValue(float v, float max)
	{
		return (sint8)std::clamp<sint32>((sint32)std::lround(v * max), (sint32)-max, (sint32)max);
	}

	void BuildStatus(sint32 chan, const GameCubePadController& controller, HPADStatus& status)
	{
		const glm::vec2 stick = controller.get_axis();
		const glm::vec2 substick = controller.get_rotation();
		const glm::vec2 triggers = controller.get_trigger();

		uint32 hold = controller.get_hpad_buttons();

		// digital directions for the sticks, like the console sets them
		constexpr float kEmulationThreshold = 0.5f;
		if (stick.x <= -kEmulationThreshold) hold |= HPAD_STICK_EMULATION_LEFT;
		if (stick.x >= kEmulationThreshold) hold |= HPAD_STICK_EMULATION_RIGHT;
		if (stick.y <= -kEmulationThreshold) hold |= HPAD_STICK_EMULATION_DOWN;
		if (stick.y >= kEmulationThreshold) hold |= HPAD_STICK_EMULATION_UP;
		if (substick.x <= -kEmulationThreshold) hold |= HPAD_SUBSTICK_EMULATION_LEFT;
		if (substick.x >= kEmulationThreshold) hold |= HPAD_SUBSTICK_EMULATION_RIGHT;
		if (substick.y <= -kEmulationThreshold) hold |= HPAD_SUBSTICK_EMULATION_DOWN;
		if (substick.y >= kEmulationThreshold) hold |= HPAD_SUBSTICK_EMULATION_UP;

		auto& channel = g_hpad.channel[chan];
		const uint32 last = channel.lastHold;
		channel.lastHold = hold;

		memset(&status, 0, sizeof(HPADStatus));
		status.hold = (sint32)hold;
		status.trigger = (sint32)(hold & ~last);
		status.release = (sint32)(~hold & last);
		status.stickX = ToStickValue(stick.x, kStickMax);
		status.stickY = ToStickValue(stick.y, kStickMax);
		status.substickX = ToStickValue(substick.x, kSubstickMax);
		status.substickY = ToStickValue(substick.y, kSubstickMax);
		status.triggerL = (uint8)std::clamp<sint32>((sint32)std::lround(triggers.x * kTriggerMax), 0, (sint32)kTriggerMax);
		status.triggerR = (uint8)std::clamp<sint32>((sint32)std::lround(triggers.y * kTriggerMax), 0, (sint32)kTriggerMax);
		status.status = HPAD_CONTROLLER_WIRED;
		status.error = HPAD_RESULT_OK;
	}

	sint32 HPADInit()
	{
		g_hpad.initialized = true;
		return HPAD_RESULT_OK;
	}

	void StopAllMotors()
	{
		for (sint32 chan = 0; chan < kChannelCount; ++chan)
		{
			if (!g_hpad.channel[chan].rumbling)
				continue;
			g_hpad.channel[chan].rumbling = false;
			if (auto controller = GetController(chan))
				controller->stop_rumble();
		}
	}

	sint32 HPADShutdown()
	{
		StopAllMotors();
		g_hpad.initialized = false;
		return HPAD_RESULT_OK;
	}

	// fills buffers[0] with the newest sample and returns the number of samples written
	sint32 HPADRead(sint32 chan, HPADStatus* buffers, sint32 count)
	{
		if (chan < 0 || chan >= kChannelCount || !buffers || count <= 0)
			return HPAD_RESULT_INVALID;

		const auto controller = GetController(chan);
		if (!controller)
		{
			g_hpad.channel[chan].lastHold = 0;
			return HPAD_RESULT_NO_CONTROLLER;
		}

		// read the current state of the mapped input devices (like VPADRead/WPADRead do)
		controller->controllers_update_states();
		BuildStatus(chan, *controller, buffers[0]);
		return 1;
	}

	sint32 HPADControlMotor(sint32 chan, uint32 command)
	{
		if (chan < 0 || chan >= kChannelCount)
			return HPAD_RESULT_INVALID;

		const auto controller = GetController(chan);
		if (!controller)
			return HPAD_RESULT_NO_CONTROLLER;

		auto& channel = g_hpad.channel[chan];
		if (command == HPAD_MOTOR_COMMAND_RUMBLE)
		{
			if (!channel.rumbling)
			{
				channel.rumbling = true;
				controller->start_rumble();
			}
		}
		else if (channel.rumbling)
		{
			channel.rumbling = false;
			controller->stop_rumble();
		}
		return HPAD_RESULT_OK;
	}

	sint32 HPADRecalibrate(sint32 chan)
	{
		if (chan < 0 || chan >= kChannelCount)
			return HPAD_RESULT_INVALID;
		return HPAD_RESULT_OK;
	}

	void* HPADSetConnectCallback(sint32 chan, void* callback)
	{
		if (chan < 0 || chan >= kChannelCount)
			return nullptr;

		auto& channel = g_hpad.channel[chan];
		void* previous = channel.connectCallback.GetPtr();
		channel.connectCallback = callback;
		// report the current state to the new callback on the next tick
		channel.reportedConnected = false;
		return previous;
	}

	sint32 HPADGetGGGGStatus(sint32 adapterIndex, HPADGGGGStatus* status)
	{
		if (adapterIndex < 0 || adapterIndex >= kAdapterCount || !status)
			return HPAD_RESULT_INVALID;

		const bool connected = IsAdapterConnected(adapterIndex);
		status->connected = connected ? 1 : 0;
		status->powerSupplyConnected = connected ? 1 : 0;
		status->active = (connected && g_hpad.initialized) ? 1 : 0;
		return HPAD_RESULT_OK;
	}

	void* HPADSetGgggConnectCallback(sint32 adapterIndex, void* callback)
	{
		if (adapterIndex < 0 || adapterIndex >= kAdapterCount)
			return nullptr;

		auto& adapter = g_hpad.adapter[adapterIndex];
		void* previous = adapter.connectCallback.GetPtr();
		adapter.connectCallback = callback;
		adapter.reportedConnected = false;
		return previous;
	}

	void* HPADSetPowerSupplyCallback(sint32 adapterIndex, void* callback)
	{
		if (adapterIndex < 0 || adapterIndex >= kAdapterCount)
			return nullptr;

		auto& adapter = g_hpad.adapter[adapterIndex];
		void* previous = adapter.powerSupplyCallback.GetPtr();
		adapter.powerSupplyCallback = callback;
		adapter.reportedPowerSupply = false;
		return previous;
	}

	void* HPADSetSamplingCallback(sint32 adapterIndex, void* callback)
	{
		if (adapterIndex < 0 || adapterIndex >= kAdapterCount)
			return nullptr;

		auto& adapter = g_hpad.adapter[adapterIndex];
		void* previous = adapter.samplingCallback.GetPtr();
		adapter.samplingCallback = callback;
		return previous;
	}

	sint32 HPADResetDevice(sint32 adapterIndex, void* callback)
	{
		if (adapterIndex < 0 || adapterIndex >= kAdapterCount)
			return HPAD_RESULT_INVALID;

		auto& adapter = g_hpad.adapter[adapterIndex];
		adapter.resetCallback = callback;
		adapter.resetPending = true;
		return HPAD_RESULT_OK;
	}

	void TickFunction(PPCInterpreter_t* hCPU)
	{
		if (g_hpad.initialized)
		{
			for (sint32 a = 0; a < kAdapterCount; ++a)
			{
				auto& adapter = g_hpad.adapter[a];
				const bool connected = IsAdapterConnected(a);

				if (adapter.resetPending)
				{
					adapter.resetPending = false;
					if (adapter.resetCallback)
					{
						cemuLog_log(LogType::InputAPI, "Calling HPADResetCallback({}, 0)", a);
						PPCCoreCallback(adapter.resetCallback, a, HPAD_RESULT_OK);
					}
				}

				if (adapter.connectCallback && adapter.reportedConnected != connected)
				{
					adapter.reportedConnected = connected;
					cemuLog_log(LogType::InputAPI, "Calling HPADGgggConnectCallback({}, {})", a, connected);
					PPCCoreCallback(adapter.connectCallback, a, connected ? 1 : 0);
				}

				if (adapter.powerSupplyCallback && adapter.reportedPowerSupply != connected)
				{
					adapter.reportedPowerSupply = connected;
					cemuLog_log(LogType::InputAPI, "Calling HPADPowerSupplyCallback({}, {})", a, connected);
					PPCCoreCallback(adapter.powerSupplyCallback, a, connected ? 1 : 0);
				}
			}

			for (sint32 chan = 0; chan < kChannelCount; ++chan)
			{
				auto& channel = g_hpad.channel[chan];
				const bool connected = GetController(chan) != nullptr;
				if (!connected)
					channel.rumbling = false;

				if (channel.connectCallback && channel.reportedConnected != connected)
				{
					channel.reportedConnected = connected;
					const sint32 status = connected ? kStatusConnected : kStatusDisconnected;
					cemuLog_log(LogType::InputAPI, "Calling HPADConnectCallback({}, {})", chan, status);
					PPCCoreCallback(channel.connectCallback, chan, status);
				}
			}

			for (sint32 a = 0; a < kAdapterCount; ++a)
			{
				auto& adapter = g_hpad.adapter[a];
				if (adapter.samplingCallback && IsAdapterConnected(a))
					PPCCoreCallback(adapter.samplingCallback, a);
			}
		}

		osLib_returnFromFunction(hCPU, 0);
	}

	void start()
	{
		coreinit::OSCreateAlarm(&g_hpad.alarm);
		const uint64 start_tick = coreinit::OSGetTime();
		const uint64 period_tick = coreinit::EspressoTime::GetTimerClock() * 8 / 1000; // the adapter samples at 125Hz
		const MPTR handler = PPCInterpreter_makeCallableExportDepr(TickFunction);
		coreinit::OSSetPeriodicAlarm(&g_hpad.alarm, start_tick, period_tick, handler);
	}

	class : public COSModule
	{
	  public:
		std::string_view GetName() override
		{
			return "nn_hpad";
		}

		void RPLMapped() override
		{
			s_hpadStaticData.Allocate();
			g_hpad.initialized = false;
			for (auto& channel : g_hpad.channel)
				channel = {};
			for (auto& adapter : g_hpad.adapter)
				adapter = {};

			cafeExportRegister("nn_hpad", HPADInit, LogType::InputAPI);
			cafeExportRegister("nn_hpad", HPADShutdown, LogType::InputAPI);
			cafeExportRegister("nn_hpad", HPADRead, LogType::InputAPI);
			cafeExportRegister("nn_hpad", HPADControlMotor, LogType::InputAPI);
			cafeExportRegister("nn_hpad", HPADRecalibrate, LogType::InputAPI);
			cafeExportRegister("nn_hpad", HPADSetConnectCallback, LogType::InputAPI);
			cafeExportRegister("nn_hpad", HPADGetGGGGStatus, LogType::InputAPI);
			cafeExportRegister("nn_hpad", HPADSetGgggConnectCallback, LogType::InputAPI);
			cafeExportRegister("nn_hpad", HPADSetPowerSupplyCallback, LogType::InputAPI);
			cafeExportRegister("nn_hpad", HPADSetSamplingCallback, LogType::InputAPI);
			cafeExportRegister("nn_hpad", HPADResetDevice, LogType::InputAPI);
		}

		void rpl_entry(uint32 moduleHandle, coreinit::RplEntryReason reason) override
		{
			if (reason == coreinit::RplEntryReason::Loaded)
				start();
		}

		void RPLUnmapped() override
		{
			coreinit::OSCancelAlarm(g_hpad.alarm.GetPtr());
			StopAllMotors();
			g_hpad.initialized = false;
			s_hpadStaticData.Deallocate();
		}
	} s_COSnnHpadModule;

	COSModule* GetModule()
	{
		return &s_COSnnHpadModule;
	}
}
