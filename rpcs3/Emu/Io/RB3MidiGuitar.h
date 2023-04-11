#pragma once

#include "Emu/Io/usb_device.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wsuggest-override"
#include <RtMidi.h>
#pragma GCC diagnostic pop

class usb_device_rb3_midi_guitar : public usb_device_emulated
{
private:
	u32 response_pos = 0;
	bool buttons_enabled = false;
	std::unique_ptr<RtMidiIn> midi_in;

	// button states
	struct
	{
		u8 count = 0;

		bool cross = false;
		bool circle = false;
		bool square = false;
		bool triangle = false;

		bool start = false;
		bool select = false;
		bool tilt_sensor = false;
		bool sustain_pedal = false; // used for overdrive

		u8 dpad = 8;

		u8 frets[6] = {0};
		u8 string_velocities[6] = {0};
	} button_state;

	void parse_midi_message(std::vector<u8>& msg);
	void write_state(u8 buf[27]);

public:
	usb_device_rb3_midi_guitar(const std::array<u8, 7>& location);
	~usb_device_rb3_midi_guitar();

	void control_transfer(u8 bmRequestType, u8 bRequest, u16 wValue, u16 wIndex, u16 wLength, u32 buf_size, u8* buf, UsbTransfer* transfer) override;
	void interrupt_transfer(u32 buf_size, u8* buf, u32 endpoint, UsbTransfer* transfer) override;
};
