#ifndef woXrooX_WiFi_H
#define woXrooX_WiFi_H

// #include <string.h>

#include "freertos/FreeRTOS.h"
// #include "freertos/task.h"
// #include "freertos/event_groups.h"

// #include "esp_system.h"
#include "esp_wifi.h"
// #include "esp_event.h"
#include "esp_log.h"

#include "nvs_flash.h"

//// DEFINES

#define WIFI_SUCCESS (1 << 0)
#define WIFI_FAILURE (1 << 1)
#define WIFI_CONNECTION_MAX_RETRY 10
#define WIFI_SSID "My_WiFi"
#define WIFI_PASS "My_Password"

//// GLOBALS

// event group to contain status information
static EventGroupHandle_t wifi_event_group;

// Retry tracker
static int retry_count = 0;

// Task tag
static const char *WiFi_TAG = "woXrooX::WiFi:";

// Event handler for wifi events
static void WiFi_event_handler(
	void* arg,
	esp_event_base_t event_base,
	int32_t event_id,
	void* event_data
) {
	if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
		ESP_LOGI(WiFi_TAG, "Connecting to AP...");
		esp_wifi_connect();
	}

	else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
		if (retry_count < WIFI_CONNECTION_MAX_RETRY) {
			ESP_LOGI(WiFi_TAG, "Retry to connect to AP...");
			esp_wifi_connect();
			retry_count++;
		}

		else { xEventGroupSetBits(wifi_event_group, WIFI_FAILURE); }
	}
}

// Event handler for ip events
static void IP_event_handler(
	void* arg,
	esp_event_base_t event_base,
	int32_t event_id,
	void* event_data
) {
	if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
		ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
		ESP_LOGI(WiFi_TAG, "STA IP: " IPSTR, IP2STR(&event->ip_info.ip));
		retry_count = 0;
		xEventGroupSetBits(wifi_event_group, WIFI_SUCCESS);
	}
}

// Wi-Fi/Netif initialisation
int init_WiFi_STA(void) {
	int status = WIFI_FAILURE;

	// Initialize the esp network interface
	esp_netif_init();

	// Initialize default esp event loop
	esp_event_loop_create_default();

	// Create wifi station in the wifi driver
	esp_netif_create_default_wifi_sta();

	// Wi-Fi driver - setup wifi station with the default wifi configuration
	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	esp_wifi_init(&cfg);

	wifi_event_group = xEventGroupCreate();

	esp_event_handler_instance_t WiFi_handler_event_instance;
	esp_event_handler_instance_register(
		WIFI_EVENT,
		ESP_EVENT_ANY_ID,
		&WiFi_event_handler,
		NULL,
		&WiFi_handler_event_instance
	);

	esp_event_handler_instance_t got_IP_event_instance;
	esp_event_handler_instance_register(
		IP_EVENT,
		IP_EVENT_STA_GOT_IP,
		&IP_event_handler,
		NULL,
		&got_IP_event_instance
	);

	wifi_config_t wifi_config = {
		.sta = {
			.ssid = WIFI_SSID,
			.password = WIFI_PASS,
			.threshold.authmode = WIFI_AUTH_WPA2_PSK,

			// WPA2/WPA3 mixed
			.sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
		},
	};

	// Set the wifi controller to be a station
	esp_wifi_set_mode(WIFI_MODE_STA);

	// Set the wifi config
	esp_wifi_set_config(WIFI_IF_STA, &wifi_config);

	// Start the wifi driver
	esp_wifi_start();

	ESP_LOGI(WiFi_TAG, "init_WiFi_STA(): finished.");

	// Wait here until either connected or failed
	EventBits_t bits = xEventGroupWaitBits(
		wifi_event_group,
		WIFI_SUCCESS | WIFI_FAILURE,
		pdFALSE,
		pdFALSE,
		portMAX_DELAY
	);

	if (bits & WIFI_SUCCESS) {
		ESP_LOGI(WiFi_TAG, "connected to ap SSID:%s  password:%s", WIFI_SSID, WIFI_PASS);
		status = WIFI_SUCCESS;
	}

	else if (bits & WIFI_FAILURE) {
		ESP_LOGI(WiFi_TAG, "Failed to connect to SSID:%s", WIFI_SSID);
		status = WIFI_FAILURE;
	}

	else {
		ESP_LOGE(WiFi_TAG, "UNEXPECTED EVENT");
		status = WIFI_FAILURE;
	}

	// The event will not be processed after unregister
	esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, WiFi_handler_event_instance);
	esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, got_IP_event_instance);
	vEventGroupDelete(wifi_event_group);

	return status;
}

int init_WiFi(void) {
	esp_err_t status = WIFI_FAILURE;

	// Initialize storage
	// Sta­ble NVS (Wi-Fi stores credentials here)
	esp_err_t ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		nvs_flash_erase();
		ret = nvs_flash_init();
	}

	// Connect to wireless AP
	status = init_WiFi_STA();
	if (status != WIFI_SUCCESS) {
		ESP_LOGI(WiFi_TAG, "Failed to associate to AP, dying...");
		return 1;
	}

	return 0;
}

#endif
