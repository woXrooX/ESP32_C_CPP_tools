#ifndef woXrooX_WebSocket_client_H
#define woXrooX_WebSocket_client_H

/*
WebSocket Audio Sender (minimal binary 652-byte frames)
Usage:

// Bring your mic header (for MIC_frame_type) before this header
// #include "woXrooX/MIC.h"


// Call once after Wi-Fi is up. Provide the mic queue (from listen_queue())
MIC_listen_start();
WS_start(MIC_listen_queue());
*/

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_websocket_client.h"
// #include "esp_tls.h"

////////////// DEFINES

#define WS_URL "ws://192.168.1.4:8080/stream"

// Optional subprotocol for versioning on the server
#define WS_SUBPROTOCOL "woXrooX.STT.v1"

// If you use WSS, embed your server/CA cert and point cert_pem to it.
// See notes below about EMBED_TXTFILES.
#define USE_WSS 0

////////////// GLOBALS

static const char *WS_TAG = "woXrooX::WS:";

static esp_websocket_client_handle_t WS_client = NULL;
static volatile bool WS_ready = false;

static QueueHandle_t WS_source_queue = NULL;

// 652 bytes = 4 + 8 + 640
#define WS_FRAME_BYTES 652
static uint8_t WS_buffer[WS_FRAME_BYTES];

////////////// PACKING (little-endian)

static inline void little_endian_32(uint8_t *p, uint32_t v) {
	p[0] = (uint8_t)(v);
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

static inline void little_endian_64(uint8_t *p, uint64_t v) {
	p[0] = (uint8_t)(v);
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
	p[4] = (uint8_t)(v >> 32);
	p[5] = (uint8_t)(v >> 40);
	p[6] = (uint8_t)(v >> 48);
	p[7] = (uint8_t)(v >> 56);
}

static void pack_frame(uint8_t *out, const MIC_frame_type *f) {
	little_endian_32(out + 0,  f->seq);
	little_endian_64(out + 4,  f->ts_us);
	memcpy(out + 12, (const void *)f->pcm, 320 * sizeof(int16_t));
}

////////////// EVENT HANDLER

static void WS_event_handler(
	void *handler_args,
	esp_event_base_t base,
	int32_t event_id,
	void *event_data
) {
	(void)handler_args;
	(void)base;

	esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

	switch (event_id) {
		case WEBSOCKET_EVENT_CONNECTED:
			WS_ready = true;
			ESP_LOGI(WS_TAG, "Connected");
			break;

		case WEBSOCKET_EVENT_DISCONNECTED:
			WS_ready = false;
			ESP_LOGW(WS_TAG, "Disconnected");
			break;

		case WEBSOCKET_EVENT_DATA:
			// Minimal sender: ignore inbound data for now (auth can be added later)
			ESP_LOGD(WS_TAG, "rx %d bytes (bin=%d, opcode=0x%x)", data->data_len, data->op_code == 2, data->op_code);
			break;

		case WEBSOCKET_EVENT_ERROR:
			WS_ready = false;
			ESP_LOGE(WS_TAG, "Error");
			break;

		default: break;
	}
}

////////////// Send a tiny JSON control message on PTT edge
static inline void WS_send_PTT_protocol(bool active) {
	if (!WS_ready || !WS_client) return;

	char buf[48];
	int n = snprintf(buf, sizeof(buf), "{\"type\":\"PTT\",\"event\":\"%s\"}", active ? "start" : "end");

	// short timeout so we never stall the button task
	esp_websocket_client_send_text(WS_client, buf, n, pdMS_TO_TICKS(50));
}


////////////// TX TASK

static void WS_tx_task(void *param) {
	(void)param;

	MIC_frame_type frame;

	while (1) {
		if (!WS_ready) {
			vTaskDelay(pdMS_TO_TICKS(50));
			continue;
		}

		if (xQueueReceive(WS_source_queue, &frame, portMAX_DELAY) != pdTRUE) continue;

		// drop this frame (keeps DMA happy, no back-pressure)
		if (!get_Button_PTT_FLAG_active()) continue;

		pack_frame(WS_buffer, &frame);

		// Send as binary WS frame
		int rc = esp_websocket_client_send_bin(WS_client, (const char *)WS_buffer, WS_FRAME_BYTES, pdMS_TO_TICKS(1000));

		// Allow reconnect logic in the client to handle it; frames will continue when WS_ready is true again
		if (rc < 0) ESP_LOGW(WS_TAG, "send_bin failed (%d), seq=%u", rc, frame.seq);
	}
}

////////////// API

static void WS_start(QueueHandle_t source_queue) {
	WS_source_queue = source_queue;

	esp_websocket_client_config_t cfg = {
		.uri = WS_URL,
		.subprotocol = WS_SUBPROTOCOL,
		.disable_auto_reconnect = false,
		.network_timeout_ms = 5000,
		.ping_interval_sec = 15
	};

	#if USE_WSS
	// Embed your CA or server cert with:
	// idf_component_register(... EMBED_TXTFILES certs/server_ca.pem)
	// extern const uint8_t server_ca_pem_start[] asm("_binary_server_ca_pem_start");
	// extern const uint8_t server_ca_pem_end[]   asm("_binary_server_ca_pem_end");
	// cfg.cert_pem = (const char *)server_ca_pem_start;
	#endif

	WS_client = esp_websocket_client_init(&cfg);
	assert(WS_client);

	esp_websocket_register_events(WS_client, WEBSOCKET_EVENT_ANY, WS_event_handler, NULL);

	ESP_ERROR_CHECK(esp_websocket_client_start(WS_client));

	xTaskCreatePinnedToCore(WS_tx_task, "WS_TX", 4096, NULL, 5, NULL, tskNO_AFFINITY);
}

#endif
