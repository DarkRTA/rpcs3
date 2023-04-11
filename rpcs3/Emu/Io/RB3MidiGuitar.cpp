// Rock Band 3 MIDI Pro Adapter Emulator (Guitar Mode)

#include "stdafx.h"
#include "RB3MidiGuitar.h"
#include "Emu/Cell/lv2/sys_usbd.h"
#include "Input/pad_thread.h"

LOG_CHANNEL(rb3_midi_guitar_log);

usb_device_rb3_midi_guitar::usb_device_rb3_midi_guitar(const std::array<u8, 7>& location)
	: usb_device_emulated(location)
{
	device = UsbDescriptorNode(USB_DESCRIPTOR_DEVICE, UsbDeviceDescriptor{0x0200, 0x00, 0x00, 0x00, 64, 0x12ba, 0x2438, 0x01, 0x01, 0x02, 0x00, 0x01});
	auto& config0 = device.add_node(UsbDescriptorNode(USB_DESCRIPTOR_CONFIG, UsbDeviceConfiguration{41, 1, 1, 0, 0x80, 32}));
	config0.add_node(UsbDescriptorNode(USB_DESCRIPTOR_INTERFACE, UsbDeviceInterface{0, 0, 2, 3, 0, 0, 0}));
	config0.add_node(UsbDescriptorNode(USB_DESCRIPTOR_HID, UsbDeviceHID{0x0111, 0x00, 0x01, 0x22, 137}));
	config0.add_node(UsbDescriptorNode(USB_DESCRIPTOR_ENDPOINT, UsbDeviceEndpoint{0x81, 0x03, 0x0040, 10}));
	config0.add_node(UsbDescriptorNode(USB_DESCRIPTOR_ENDPOINT, UsbDeviceEndpoint{0x02, 0x03, 0x0040, 10}));

	char str1[] = "Licensed by Sony Computer Entertainment America";
	char str2[] = "Harmonix RB3 MIDI Guitar Interface for PlayStation®3";
	usb_device_emulated::add_string(str1);
	usb_device_emulated::add_string(str2);

	// set up midi input
	midi_in = std::make_unique<RtMidiIn>();
	midi_in->ignoreTypes(false, true, true);
	midi_in->openVirtualPort("RPCS3 Guitar Midi In");
	rb3_midi_guitar_log.success("creating midi port");
}

usb_device_rb3_midi_guitar::~usb_device_rb3_midi_guitar()
{
}

static u8 disabled_response[] = {
	0xe9, 0x00, 0x00, 0x00, 0x00, 0x02, 0x0f, 0x01,
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

void usb_device_rb3_midi_guitar::control_transfer(u8 bmRequestType, u8 bRequest, u16 wValue, u16 wIndex, u16 wLength, u32 buf_size, u8* buf, UsbTransfer* transfer)
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
			rb3_midi_guitar_log.notice("MIDI data enabled.");
			buttons_enabled = true;
			response_pos = 0;
			break;
		case 0x81:
			rb3_midi_guitar_log.notice("MIDI data disabled.");
			buttons_enabled = false;
			response_pos = 0;
			break;
		default:
			rb3_midi_guitar_log.warning("Unhandled SET_REPORT request: 0x%02X");
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

void usb_device_rb3_midi_guitar::interrupt_transfer(u32 buf_size, u8* buf, u32 /*endpoint*/, UsbTransfer* transfer)
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

	// default input state
	u8 bytes[27] = {
		0x00, 0x00, 0x08, 0x80, 0x80, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40,
		0x40, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00};

	memcpy(buf, bytes, 27);

	write_state(buf);
}

void usb_device_rb3_midi_guitar::parse_midi_message(std::vector<u8>& msg)
{
	// this is not emulated correctly but the game doesn't seem to care
	button_state.count++;

	rb3_midi_guitar_log.success("msg: %d", msg[0]);
	// read frets
	if (msg.size() == 8 && msg[0] == 0xF0 && msg[4] == 0x01)
	{
		switch (msg[5])
		{
		case 1:
			button_state.frets[0] = msg[6] - 0x40;
			break;
		case 2:
			button_state.frets[1] = msg[6] - 0x3B;
			break;
		case 3:
			button_state.frets[2] = msg[6] - 0x37;
			break;
		case 4:
			button_state.frets[3] = msg[6] - 0x32;
			break;
		case 5:
			button_state.frets[4] = msg[6] - 0x2D;
			break;
		case 6:
			button_state.frets[5] = msg[6] - 0x28;
			break;
		}
	}

	// read strings
	if (msg.size() == 8 && msg[0] == 0xF0 && msg[4] == 0x05)
	{
		button_state.string_velocities[msg[5] - 1] = msg[6];
	}

	// read buttons
	if (msg.size() == 10 && msg[0] == 0xF0 && msg[4] == 0x08)
	{
		// dpad

		button_state.dpad = msg[7] & 0x0f;

		button_state.square = (msg[5] & 0b0000'0001) == 0b0000'0001;
		button_state.cross = (msg[5] & 0b0000'0010) == 0b0000'0010;
		button_state.circle = (msg[5] & 0b0000'0100) == 0b0000'0100;
		button_state.triangle = (msg[5] & 0b0000'1000) == 0b0000'1000;

		button_state.select = (msg[6] & 0b0000'0001) == 0b0000'0001;
		button_state.start = (msg[6] & 0b0000'0010) == 0b0000'0010;
		button_state.tilt_sensor = (msg[7] & 0b0100'0000) == 0b0100'0000;
	}

	// sustain pedal
	if (msg.size() == 3 && msg[0] == 0xB0 && msg[1] == 0x40) {
		button_state.sustain_pedal = msg[2] >= 40;
	}
}

void usb_device_rb3_midi_guitar::write_state(u8 buf[27])
{
	// encode frets
	buf[8] |= (button_state.frets[0] & 0b11111) << 2;
	buf[8] |= (button_state.frets[1] & 0b11000) >> 3;
	buf[7] |= (button_state.frets[1] & 0b00111) << 5;
	buf[7] |= (button_state.frets[2] & 0b11111) >> 0;
	buf[6] |= (button_state.frets[3] & 0b11111) << 2;
	buf[6] |= (button_state.frets[4] & 0b11000) >> 3;
	buf[5] |= (button_state.frets[4] & 0b00111) << 5;
	buf[5] |= (button_state.frets[5] & 0b11111) >> 0;

	// encode strings
	buf[14] = button_state.string_velocities[0];
	buf[13] = button_state.string_velocities[1];
	buf[12] = button_state.string_velocities[2];
	buf[11] = button_state.string_velocities[3];
	buf[10] = button_state.string_velocities[4];
	buf[9] = button_state.string_velocities[5];

	// encode tilt sensor/sustain_pedal
	if (button_state.tilt_sensor || button_state.sustain_pedal) {
		buf[15] = 0x7f;
		buf[16] = 0x7f;
		buf[17] = 0x7f;
	}

	buf[1] |= 0b0000'0001 * button_state.select;
	buf[1] |= 0b0000'0010 * button_state.start;

	buf[0] |= 0b0000'0010 * button_state.cross;
	buf[0] |= 0b0000'0100 * button_state.circle;
	buf[0] |= 0b0000'1000 * button_state.triangle;
	buf[0] |= 0b0000'0001 * button_state.square;

	buf[2] = button_state.dpad;
}
