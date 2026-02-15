
# ESP32 Font Tester

This app allows you to preview and test different font sets on an ESP32 device with a TFT display.
It loads font files from configurable folders and displays sample text for each font size.
Touch input lets you cycle through available fonts interactively.
Ideal for developers customizing or selecting fonts for embedded display projects.


## How to Proceed

1. Add one or more `.ttf` font files to the `PythonApps/fontMaker` folder.
2. Run `fontMaker.py` to generate the font headers.
3. In `font_tester.cpp`, set the font set folder by editing:
	```cpp
	#define FONT_SET_DIR Generated:Fonts_For_Copy/IBMPlexMono_ExtraLight
	```
4. Build the project and upload it to your ESP32 device.
5. Use the touchscreen to cycle through and preview the fonts.