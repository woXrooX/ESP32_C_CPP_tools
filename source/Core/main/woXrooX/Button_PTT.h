#ifndef woXrooX_Button_PTT_H
#define woXrooX_Button_PTT_H

#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "esp_log.h"


////////////// DEFINES

#define BUTTON_PIN GPIO_NUM_27


////////////// GLOBALS

static const char *BUTTON_TAG = "woXrooX::BUTTON:";

// Exposed PTT (push-to-talks) flag (read by other tasks)
// 1 = pressed/hold,
// 0 = released
static volatile int Button_PTT_FLAG_active = 0;


////////////// Forward declarations

// static inline void WS_send_PTT_protocol(bool active);


////////////// Helpers

static inline bool get_Button_PTT_FLAG_active(void) { return Button_PTT_FLAG_active != 0; }

// Background task: debounced PRESS/RELEASE + set flag
static void button_task(void *arg) {

	// idle high (pull-up)
	int last = 1;

	for (;;) {
		int level = gpio_get_level(BUTTON_PIN);

		if (level != last) {
			// debounce
			vTaskDelay(pdMS_TO_TICKS(20));

			level = gpio_get_level(BUTTON_PIN);

			if (level != last) {
				last = level;

				if (level == 0) {
					Button_PTT_FLAG_active = 1;
					ESP_LOGI(BUTTON_TAG, "PRESS");
					LED_RED_on(500, -1);
					// WS_send_PTT_protocol(true);
				}

				else {
					Button_PTT_FLAG_active = 0;
					ESP_LOGI(BUTTON_TAG, "RELEASE");
					LED_RED_off();
					// WS_send_PTT_protocol(false);
				}
			}
		}

		vTaskDelay(pdMS_TO_TICKS(5));
	}
}

// Helper to start the task from app_main (or your init)
static void button_start_task(void) {
	xTaskCreate(button_task, "Button_PTT_task", 2048, NULL, 5, NULL);
}

////////////// API

static void button_init(void) {
	gpio_config_t io = {
		.pin_bit_mask = 1ULL << BUTTON_PIN,
		.mode = GPIO_MODE_INPUT,
		.pull_up_en = GPIO_PULLUP_ENABLE,
		.pull_down_en = GPIO_PULLDOWN_DISABLE,
		.intr_type = GPIO_INTR_DISABLE,
	};
	gpio_config(&io);

	button_start_task();
}

#endif
