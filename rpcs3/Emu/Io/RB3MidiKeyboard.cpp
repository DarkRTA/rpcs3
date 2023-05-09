// Rock Band 3 MIDI Pro Adapter Emulator (Keyboard Mode)

#include "stdafx.h"
#include "RB3MidiKeyboard.h"
#include "Emu/Cell/lv2/sys_usbd.h"

LOG_CHANNEL(rb3_midi_keyboard_log);

usb_device_rb3_midi_keyboard::usb_device_rb3_midi_keyboard(const std::array<u8, 7>& location)
	: usb_device_emulated(location)
{
	device = UsbDescriptorNode(USB_DESCRIPTOR_DEVICE, UsbDeviceDescriptor{0x0200, 0x00, 0x00, 0x00, 64, 0x12ba, 0x2338, 0x01, 0x01, 0x02, 0x00, 0x01});
	auto& config0 = device.add_node(UsbDescriptorNode(USB_DESCRIPTOR_CONFIG, UsbDeviceConfiguration{41, 1, 1, 0, 0x80, 32}));
	config0.add_node(UsbDescriptorNode(USB_DESCRIPTOR_INTERFACE, UsbDeviceInterface{0, 0, 2, 3, 0, 0, 0}));
	config0.add_node(UsbDescriptorNode(USB_DESCRIPTOR_HID, UsbDeviceHID{0x0111, 0x00, 0x01, 0x22, 137}));
	config0.add_node(UsbDescriptorNode(USB_DESCRIPTOR_ENDPOINT, UsbDeviceEndpoint{0x81, 0x03, 0x0040, 10}));
	config0.add_node(UsbDescriptorNode(USB_DESCRIPTOR_ENDPOINT, UsbDeviceEndpoint{0x02, 0x03, 0x0040, 10}));

	char str1[] = "Licensed by Sony Computer Entertainment America";
	char str2[] = "Harmonix RB3 MIDI Keyboard Interface for PlayStation®3";
	usb_device_emulated::add_string(str1);
	usb_device_emulated::add_string(str2);

	// set up midi input
	midi_in = rtmidi_in_create_default();
	rtmidi_open_virtual_port(midi_in, "RPCS3 Keyboard Midi In");
	rb3_midi_keyboard_log.success("creating midi port");
}

usb_device_rb3_midi_keyboard::~usb_device_rb3_midi_keyboard()
{
	rtmidi_in_free(midi_in);
}

static u8 disabled_response[] = {
	0xe9, 0x00, 0x00, 0x00, 0x00, 0x02, 0x0d, 0x01,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x82,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x21, 0x26, 0x02, 0x06, 0x00, 0x00, 0x00, 0x00};

static u8 enabled_response[] = {
	0xe9, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x8a,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x21, 0x26, 0x02, 0x06, 0x00, 0x00, 0x00, 0x00};

void usb_device_rb3_midi_keyboard::control_transfer(u8 bmRequestType, u8 bRequest, u16 wValue, u16 wIndex, u16 wLength, u32 buf_size, u8* buf, UsbTransfer* transfer)
{
	transfer->fake = true;

	// configuration packets sent by rock band 3
	// we only really need to check 1 byte here to figure out if the game
	// wants to enable midi data or disable it
	if (bmRequestType == 0x21 && bRequest == 0x9 && wLength == 40)
	{
		switch (buf[2])
		{
		case 0x89:
			rb3_midi_keyboard_log.notice("MIDI data enabled.");
			buttons_enabled = true;
			response_pos = 0;
			break;
		case 0x81:
			rb3_midi_keyboard_log.notice("MIDI data disabled.");
			buttons_enabled = false;
			response_pos = 0;
			break;
		default:
			rb3_midi_keyboard_log.warning("Unhandled SET_REPORT request: 0x%02X");
			break;
		}
	}
	// the game expects some sort of response to the configutarion packet
	else if (bmRequestType == 0xa1 && bRequest == 0x1)
	{
		transfer->expected_count = buf_size;
		// TODO: ensure memory safety
		if (buttons_enabled)
		{
			memcpy(buf, &enabled_response[response_pos], buf_size);
			response_pos += buf_size;
		}
		else
		{
			memcpy(buf, &disabled_response[response_pos], buf_size);
			response_pos += buf_size;
		}
	}
	else if (bmRequestType == 0x21 && bRequest == 0x9 && wLength == 8)
	{
		// the game uses this request to do things like set the LEDs
		// we don't have any LEDs, so do nothing
	}
	else
	{
		usb_device_emulated::control_transfer(bmRequestType, bRequest, wValue, wIndex, wLength, buf_size, buf, transfer);
	}
}

void usb_device_rb3_midi_keyboard::interrupt_transfer(u32 buf_size, u8* buf, u32 /*endpoint*/, UsbTransfer* transfer)
{
	transfer->fake = true;
	transfer->expected_count = buf_size;
	transfer->expected_result = HC_CC_NOERR;
	// the real device takes 8ms to send a response, but there is
	// no reason we can't make it faster
	transfer->expected_time = get_timestamp() + 1'000;

	// default input state
	const u8 bytes[27] = {
		0x00, 0x00, 0x08, 0x80, 0x80, 0x80, 0x80, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x02, 0x00,
		0x02, 0x00, 0x02};

	if (buf_size < 27)
	{
		rb3_midi_keyboard_log.warning("buffer size < 27 bytes. bailing out early");
		return;
	}

	memcpy(buf, bytes, 27);

	while (true)
	{
		u8 midi_msg[32];
		usz size = sizeof(midi_msg);

		// this returns a double as some sort of delta time, with -1.0
		// being used to signal an error
		if (rtmidi_in_get_message(midi_in, midi_msg, &size) == -1.0)
		{
			rb3_midi_keyboard_log.error("Error getting midi message");
			return;
		}

		if (size == 0)
		{
			break;
		}

		parse_midi_message(midi_msg, size);
	}

	write_state(buf);
}

void usb_device_rb3_midi_keyboard::parse_midi_message(u8 msg[32], usz size)
{
	// this is not properly emulated but the game really does not seem
	// to care
	button_state.count++;

	// handle note on/off messages
	if ((msg[0] == 0x80 || msg[0] == 0x90) && size == 3)
	{
		// handle navigation buttons
		switch (msg[1])
		{
		case 44: // G#2
			button_state.cross = ((0x10 & msg[0]) == 0x10);
			break;
		case 42: // F#2
			button_state.circle = ((0x10 & msg[0]) == 0x10);
			break;
		case 39: // D#2
			button_state.square = ((0x10 & msg[0]) == 0x10);
			break;
		case 37: // C#2
			button_state.triangle = ((0x10 & msg[0]) == 0x10);
			break;
		case 46: // A#2
			button_state.start = ((0x10 & msg[0]) == 0x10);
			break;
		case 36: // C2
			button_state.select = ((0x10 & msg[0]) == 0x10);
			break;
		case 45: // A2
			button_state.overdrive = ((0x10 & msg[0]) == 0x10);
			break;
		case 41: // F2
			button_state.dpad_up = ((0x10 & msg[0]) == 0x10);
			break;
		case 43: // G2
			button_state.dpad_down = ((0x10 & msg[0]) == 0x10);
			break;
		case 38: // D2
			button_state.dpad_left = ((0x10 & msg[0]) == 0x10);
			break;
		case 40: // E2
			button_state.dpad_right = ((0x10 & msg[0]) == 0x10);
			break;
		default:
			break;
		}

		// handle keyboard keys
		if (msg[1] >= 48 && msg[1] <= 72)
		{
			const u32 key = msg[1] - 48;
			button_state.keys[key] = ((0x10 & msg[0]) == 0x10);
			button_state.velocities[key] = msg[2];
		}
	}

	// control channel for overdrive
	if (msg[0] == 0xB0 && size == 3)
	{
		switch (msg[1])
		{
		case 0x1:
		case 0x40:
			button_state.overdrive = msg[2] > 40;
			break;
		default:
			break;
		}
	}

	// pitch wheel
	if (msg[0] == 0xE0 && size == 3)
	{
		const u16 msb = msg[2];
		const u16 lsb = msg[1];
		button_state.pitch_wheel = (msb << 7) | lsb;
	}
}

void usb_device_rb3_midi_keyboard::write_state(u8 buf[27])
{
	// buttons
	buf[0] |= 0b0000'0010 * button_state.cross;
	buf[0] |= 0b0000'0100 * button_state.circle;
	buf[0] |= 0b0000'0001 * button_state.square;
	buf[0] |= 0b0000'1000 * button_state.triangle;
	buf[1] |= 0b0000'0010 * button_state.start;
	buf[1] |= 0b0000'0001 * button_state.select;

	// dpad
	if (button_state.dpad_up)
	{
		buf[2] = 0;
	}
	else if (button_state.dpad_down)
	{
		buf[2] = 4;
	}
	else if (button_state.dpad_left)
	{
		buf[2] = 6;
	}
	else if (button_state.dpad_right)
	{
		buf[2] = 2;
	}

	// build key bitfield and write velocities
	u32 key_mask = 0;
	u8 vel_idx = 0;

	for (u32 i = 0; i < 25; i++)
	{
		key_mask <<= 1;
		key_mask |= 0x1 * button_state.keys[i];

		// the keyboard can only report 5 velocities from left to right
		if (button_state.keys[i] && vel_idx < 5)
		{
			buf[8 + vel_idx++] = button_state.velocities[i];
		}
	}

	// write keys
	buf[5] = (key_mask >> 17) & 0xff;
	buf[6] = (key_mask >> 9) & 0xff;
	buf[7] = (key_mask >> 1) & 0xff;
	buf[8] |= 0b1000'0000 * (key_mask & 0x1);

	// overdrive
	buf[13] |= 0b1000'0000 * button_state.overdrive;

	// pitch wheel
	const u8 wheel_pos = std::abs((button_state.pitch_wheel >> 6) - 0x80);
	if (wheel_pos >= 5)
	{
		buf[15] = std::min<u8>(std::max<u8>(0x5, wheel_pos), 0x75);
	}
}
