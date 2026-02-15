#include <Arduino.h>
#include <esp_chip_info.h>
#include <esp_flash.h>
#include <esp_system.h>
#include <rom/rtc.h>
#include "HB9IIU_SystemInformation.h"

void HB9IIUPrintSystemInformation() {
	esp_chip_info_t chip_info;
	esp_chip_info(&chip_info);
	uint32_t flash_size = 0;
	esp_flash_get_size(NULL, &flash_size);

	Serial.println();
	Serial.println("══════════════════════════════════════════════");
	Serial.println("  ESP32 System Information");
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
	Serial.printf("Sketch size     : %u bytes\n", ESP.getSketchSize());
	Serial.printf("Sketch free     : %u bytes\n", ESP.getFreeSketchSpace());
	Serial.printf("CPU frequency   : %u MHz\n", ESP.getCpuFreqMHz());
	Serial.printf("SDK version     : %s\n", ESP.getSdkVersion());
#ifdef ARDUINO
	Serial.printf("Arduino version : %s\n", ARDUINO_VERSION_BUILD);
#endif
	Serial.printf("Uptime          : %.2f seconds\n", millis() / 1000.0);
	Serial.printf("Reset reason    : %s\n", esp_reset_reason() == ESP_RST_POWERON ? "Power-on" :
		esp_reset_reason() == ESP_RST_SW ? "Software" :
		esp_reset_reason() == ESP_RST_DEEPSLEEP ? "Deep Sleep" :
		esp_reset_reason() == ESP_RST_BROWNOUT ? "Brownout" :
		esp_reset_reason() == ESP_RST_WDT ? "Watchdog" : "Other");

	// Temperature (if supported)
#ifdef TEMPERATURE_SENSOR
	// Not all ESP32s have this, placeholder for user to implement if needed
	Serial.printf("Temperature     : N/A\n");
#endif
	// Voltage (not directly available, placeholder)
	Serial.printf("Voltage         : N/A\n");

	Serial.println("══════════════════════════════════════════════");
}
