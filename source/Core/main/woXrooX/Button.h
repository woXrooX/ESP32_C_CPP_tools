/*

Usage:

static button_type button;

static void pressed_callback(button_type *button) { }
static void released_callback(button_type *button) { }

button.pin = GPIO_NUM_0;
button.on_press = pressed_callback;
button.on_release = released_callback;

Button_task_start(&button);

Or without using callbacks, handle events

for (;;) {
	if (button.pressed) { }
	vTaskDelay(pdMS_TO_TICKS(10));
}

*/


#ifndef woXrooX_Button_H
#define woXrooX_Button_H

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "esp_log.h"


////////////// DEFINES


////////////// GLOBALS

static const char *BUTTON_TAG = "woXrooX::BUTTON:";


////////////// Helpers

typedef struct button_type {
	// which GPIO
	gpio_num_t pin;

	// Debounced raw level (0/1)
	volatile uint8_t level;

	// current state (true/false)
	// 0 = released
	// 1 = pressed
	volatile bool pressed;

	void (*on_press)(struct button_type *button);
	void (*on_release)(struct button_type *button);
} button_type;



// Background task: debounced PRESS/RELEASE + set flag
static void Button_task(void *arg) {
	button_type *button = (button_type*)arg;

	int last = gpio_get_level(button->pin);
	button->level = (uint8_t)last;

	// Active-low
	button->pressed = (last == 0);

	for (;;) {
		int level = gpio_get_level(button->pin);

		if (level != last) {

			// Debounce 20ms
			vTaskDelay(pdMS_TO_TICKS(20));

			level = gpio_get_level(button->pin);

			if (level != last) {
				last = level;

				button->level = (uint8_t)level;
				bool new_pressed = (level == 0);

				// Only act on edge
				if (new_pressed != button->pressed) {
					button->pressed = new_pressed;

					if (button->pressed) {
						if (button->on_press) button->on_press(button);
					}

					else {
						if (button->on_release) button->on_release(button);
					}
				}
			}
		}

		vTaskDelay(pdMS_TO_TICKS(5));
	}
}


////////////// API

static void Button_task_start(button_type *button) {
	// Configure GPIO using button->pin
	gpio_config_t io = {
		.pin_bit_mask = 1ULL << button->pin,
		.mode = GPIO_MODE_INPUT,
		.pull_up_en = GPIO_PULLUP_ENABLE,
		.pull_down_en = GPIO_PULLDOWN_DISABLE,
		.intr_type = GPIO_INTR_DISABLE,
	};
	gpio_config(&io);

	// start task (PASS THE POINTER)
	xTaskCreate(Button_task, "Button_task", 2048, (void*)button, 5, NULL);
}

#endif
