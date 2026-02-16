#include <Arduino.h>
#include <LittleFS.h>
#include <esp_chip_info.h>
#include <esp_flash.h>
#include <esp_system.h>
#include <rom/rtc.h>
#include "HB9IIU_SystemInformation.h"

// Recursively list files and directories in LittleFS with classic tree look and emojis
void listDir(fs::FS &fs, const char * dirname, uint8_t levels, String prefix = "") {
	File root = fs.open(dirname);
	if (!root) {
		Serial.println("Failed to open directory");
		return;
	}
	if (!root.isDirectory()) {
		Serial.println("Not a directory");
		return;
	}
	// Count entries for tree drawing
	int entryCount = 0;
	File tmp = root.openNextFile();
	while (tmp) {
		entryCount++;
		tmp = root.openNextFile();
	}
	root.rewindDirectory();
	int i = 0;
	File file = root.openNextFile();
	while (file) {
		i++;
		bool isLast = (i == entryCount);
		String branch = prefix + (isLast ? "└── " : "├── ");
		if (file.isDirectory()) {
			Serial.print(branch);
			Serial.print("📁 ");
			Serial.println(file.name());
			if (levels) {
				String newPrefix = prefix + (isLast ? "    " : "│   ");
				listDir(fs, file.path(), levels - 1, newPrefix);
			}
		} else {
			Serial.print(branch);
			Serial.print("📄 ");
			Serial.print(file.name());
			Serial.print("  SIZE: ");
			Serial.println(file.size());
		}
		file = root.openNextFile();
	}
}

void HB9IIUPrintSystemInformation() {
	esp_chip_info_t chip_info;
	esp_chip_info(&chip_info);
	uint32_t flash_size = 0;
	esp_flash_get_size(NULL, &flash_size);

	Serial.println();
	Serial.println("══════════════════════════════════════════════");
	Serial.println("  ESP32 System Information 🖥️");
	Serial.println("══════════════════════════════════════════════");
	Serial.printf("Chip model      : ESP32-%s\n", chip_info.model == CHIP_ESP32 ? "D0WDQ6" : "UNKNOWN");
	Serial.printf("Revision        : %d\n", chip_info.revision);
	Serial.printf("Cores           : %d\n", chip_info.cores);
	Serial.printf("Features        : %s%s%s\n",
		(chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi/" : "",
		(chip_info.features & CHIP_FEATURE_BLE) ? "BLE/" : "",
		(chip_info.features & CHIP_FEATURE_BT) ? "BT" : "");
	Serial.printf("Flash size      : %u MB\n", flash_size / (1024 * 1024));
	Serial.printf("Flash speed     : %u Hz\n", ESP.getFlashChipSpeed());
	Serial.printf("Flash mode      : %s\n", ESP.getFlashChipMode() == FM_QIO ? "QIO" : "DIO");
	Serial.printf("Free heap       : %u bytes\n", ESP.getFreeHeap());
#ifdef BOARD_HAS_PSRAM
	Serial.printf("Free PSRAM      : %u bytes\n", ESP.getFreePsram());
#endif
	uint32_t sketch_size = ESP.getSketchSize();
	uint32_t sketch_free = ESP.getFreeSketchSpace();
	float sketch_free_percent = (sketch_free + sketch_size) > 0 ? (100.0f * sketch_free) / (sketch_free + sketch_size) : 0.0f;
	Serial.printf("Sketch size     : %u bytes\n", sketch_size);
	Serial.printf("Sketch free     : %u bytes (%.1f%%)\n", sketch_free, sketch_free_percent);
	Serial.printf("CPU frequency   : %u MHz\n", ESP.getCpuFreqMHz());
	Serial.printf("SDK version     : %s\n", ESP.getSdkVersion());
	Serial.printf("Uptime          : %.2f seconds\n", millis() / 1000.0);
	Serial.println("══════════════════════════════════════════════");

	Serial.println("  LittleFS Filesystem 📦");
	Serial.println("══════════════════════════════════════════════");
	if (LittleFS.begin()) {
		uint32_t total = LittleFS.totalBytes();
		uint32_t used = LittleFS.usedBytes();
		uint32_t free = total > used ? total - used : 0;
		float free_percent = total > 0 ? (100.0f * free) / total : 0.0f;
		Serial.printf("Total space     : %u bytes\n", total);
		Serial.printf("Used space      : %u bytes\n", used);
		Serial.printf("Free space      : %u bytes (%.1f%%)\n", free, free_percent);
		Serial.println("Filesystem tree:");
		listDir(LittleFS, "/", 3, "");
		LittleFS.end();
	} else {
		Serial.println("LittleFS        : Mount failed");
	}
	Serial.println("══════════════════════════════════════════════");

}

void setup() {
	Serial.begin(115200);
	delay(1000); // Wait for Serial to initialize
	HB9IIUPrintSystemInformation();
}

void loop() {
	// put your main code here, to run repeatedly:
}
