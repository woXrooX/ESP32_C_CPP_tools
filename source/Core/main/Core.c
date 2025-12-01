#include "woXrooX/LED_LOGGER.h"
#include "woXrooX/WiFi.h"
#include "woXrooX/Button.h"
// #include "woXrooX/MIC.h"
// #include "woXrooX/WebSocket_client.h"
// #include "woXrooX/HTTP_client.h"

void app_main(void) {
	if (LEDs_init() != 0) return;

	// if (init_WiFi() != 0) return;

	// MIC_listen_start();

	// WS_start(MIC_listen_queue());
}
