//
//
#ifndef __USBKEYBOARD_H__
#define __USBKEYBOARD_H__

#include "USB.h"
#include "USBHIDMouse.h"
#include "USBHIDKeyboard.h"

namespace USBKeyboard
{

	USBHIDMouse Mouse;
	USBHIDKeyboard Keyboard;

	void begin()
	{
		Mouse.begin();
		Keyboard.begin();
		USB.begin();
	}

	void write(uint8_t c)
	{
		Keyboard.write(c);
	}

	void writeCTRL(uint8_t c)
	{
		Keyboard.press(KEY_LEFT_CTRL);
		Keyboard.write(c);
		Keyboard.releaseAll();
	}
	void test()
	{
		Keyboard.press(KEY_LEFT_GUI);
		Mouse.move(-100, -100);
		Mouse.click(MOUSE_LEFT);
		Keyboard.write('m');
	}
} // namespace

#endif
