#ifndef SYSTEM_H
#define SYSTEM_H

#include "esp_ota_ops.h"
#include "esp_partition.h"
#include <Arduino.h>

void returnToMenu() {
  Serial.println("Returning to Main Menu...");

  // Find the factory or first OTA partition (usually app0 / ota_0)
  // In the provided partitions.csv: app0,app,ota_0,0x10000,0x280000,
  const esp_partition_t *menuPart = esp_partition_find_first(
      ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);

  if (menuPart != NULL) {
    Serial.printf("Found menu partition at 0x%x\n", menuPart->address);
    esp_err_t err = esp_ota_set_boot_partition(menuPart);
    if (err == ESP_OK) {
      Serial.println("Boot partition set. Restarting...");
      delay(100);
      esp_restart();
    } else {
      Serial.printf("Failed to set boot partition: %d\n", err);
    }
  } else {
    Serial.println("Error: Menu partition (ota_0) not found!");
  }
}

#endif
