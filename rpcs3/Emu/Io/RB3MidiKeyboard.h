#pragma once

#include "Emu/Io/usb_device.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wsuggest-override"
#include <RtMidi.h>
#pragma GCC diagnostic pop

class usb_device_rb3_midi_keyboard : public usb_device_emulated
{
private:
	u32 response_pos = 0;
	bool buttons_enabled = false;
	std::unique_ptr<RtMidiIn> midi_in;

	// button states

	// TODO: emulate velocity
	struct {
		u8 count = 0;

		bool cross = false;
		bool circle = false;
		bool start = false;
		bool select = false;
		bool overdrive = false;
		bool dpad_up = false;
		bool dpad_down = false;

		bool keys[25] = {false};
		u16 pitch_wheel = 0;
	} button_state;

	void parse_midi_message(std::vector<u8> &msg);
	void write_state(u8 buf[27]);
public:
	usb_device_rb3_midi_keyboard(const std::array<u8, 7>& location);
	~usb_device_rb3_midi_keyboard();

	void control_transfer(u8 bmRequestType, u8 bRequest, u16 wValue, u16 wIndex, u16 wLength, u32 buf_size, u8* buf, UsbTransfer* transfer) override;
	void interrupt_transfer(u32 buf_size, u8* buf, u32 endpoint, UsbTransfer* transfer) override;
};
