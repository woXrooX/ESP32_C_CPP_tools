#include "woXrooX/LED_LOGGER.h"
#include "woXrooX/WiFi.h"
#include "woXrooX/Button_PTT.h"
#include "woXrooX/MIC.h"
#include "woXrooX/WebSocket_client.h"
// #include "woXrooX/HTTP_client.h"

void app_main(void) {
	if (LEDs_init() != 0) return;

	// if (init_WiFi() != 0) return;

	button_init();

	// MIC_listen_start();

	// WS_start(MIC_listen_queue());
}
