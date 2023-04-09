// Guitar Hero Live controller emulator

#include "stdafx.h"
#include "RB3MidiKeyboard.h"
#include "Emu/Cell/lv2/sys_usbd.h"
#include "Input/pad_thread.h"

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

	midi_in = std::make_unique<RtMidiIn>();

	midi_in->openVirtualPort("RPCS3 Midi In");
	rb3_midi_keyboard_log.success("creating midi port");
}

usb_device_rb3_midi_keyboard::~usb_device_rb3_midi_keyboard()
{
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

extern bool is_input_allowed();

void usb_device_rb3_midi_keyboard::interrupt_transfer(u32 buf_size, u8* buf, u32 /*endpoint*/, UsbTransfer* transfer)
{
	transfer->fake = true;
	transfer->expected_count = buf_size;
	transfer->expected_result = HC_CC_NOERR;
	// the real device takes 8ms to send a response, but there is
	// no reason we can't make it faster
	transfer->expected_time = get_timestamp() + 1'000;

	memset(buf, 0, buf_size);

	std::vector<u8> midi_msg;

	midi_in->getMessage(&midi_msg);
	while (midi_msg.size() != 0)
	{
		parse_midi_message(midi_msg);
		midi_in->getMessage(&midi_msg);
	}

	buf[0] = 0x00;
	buf[1] = 0x00;
	buf[2] = 0x08;
	buf[3] = 0x80;
	buf[4] = 0x80;
	buf[5] = 0x80;
	buf[6] = 0x80;
	buf[7] = 0x00;
	buf[8] = 0x00;
	buf[9] = 0x00;
	buf[10] = 0x00;
	buf[11] = 0x00;
	buf[12] = 0x00;
	buf[13] = 0x00;
	buf[14] = 0x00;
	buf[15] = 0x00;
	buf[16] = 0x00;
	buf[17] = 0x00;
	buf[18] = 0x00;
	buf[19] = 0x00;
	buf[20] = 0x02;
	buf[21] = 0x00;
	buf[22] = 0x02;
	buf[23] = 0x00;
	buf[24] = 0x02;
	buf[25] = 0x00;
	buf[26] = 0x02;

	write_state(buf);
}

void usb_device_rb3_midi_keyboard::parse_midi_message(std::vector<u8>& msg)
{
	// TODO: properly emulate the press/release count
	button_state.count++;
	switch (msg[0])
	{
	case 0x80: // note off ch1
	case 0x90: // note on ch1
		switch (msg[1])
		{
		case 45:
			button_state.cross = ((0x10 & msg[0]) == 0x10);
			break;
		case 46:
			button_state.circle = ((0x10 & msg[0]) == 0x10);
			break;
		case 44:
			button_state.start = ((0x10 & msg[0]) == 0x10);
			break;
		case 42:
			button_state.select = ((0x10 & msg[0]) == 0x10);
			break;
		case 47:
			button_state.overdrive = ((0x10 & msg[0]) == 0x10);
			break;
		case 41:
			button_state.dpad_up = ((0x10 & msg[0]) == 0x10);
			break;
		case 43:
			button_state.dpad_down = ((0x10 & msg[0]) == 0x10);
			break;
		}
		if (msg[1] >= 48 && msg[1] <= 72) {
			u32 key = msg[1] - 48;
			button_state.keys[key] = ((0x10 & msg[0]) == 0x10);
		}
		break;
	}
}

void usb_device_rb3_midi_keyboard::write_state(u8 buf[27]) {
	// buttons
	buf[0] |= 0b0000'0010 * button_state.cross;
	buf[0] |= 0b0000'0100 * button_state.circle;
	buf[1] |= 0b0000'0010 * button_state.start;
	buf[1] |= 0b0000'0001 * button_state.select;


	// dpad
	if (button_state.dpad_up) {
		buf[2] = 0;
	} else if (button_state.dpad_down) {
		buf[2] = 4;
	}

	u32 key_mask = 0;
	// build key mask
	for (u32 i = 0; i < 25; i++) {
		key_mask <<= 1;
		key_mask |= 0x1 * button_state.keys[i];
	}

	buf[5] = (key_mask >> 17) & 0xff;
	buf[6] = (key_mask >> 9) & 0xff;
	buf[7] = (key_mask >> 1) & 0xff;
	buf[8] |= 0b1000'0000 * (key_mask & 0x1);



	// overdrive
	buf[13] |= 0x80 * button_state.overdrive;
}
