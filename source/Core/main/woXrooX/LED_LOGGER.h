/*
Usage:
#include "woXrooX/LED_LOGGER.h"

if (LEDs_init() != 0) return;

LED_RED_on(200, 20);
LED_ORANGE_on(500, 10);
LED_GREEN_on(100, 40);

// Manual switch off
LED_RED_off();
LED_ORANGE_off();
LED_GREEN_off();
*/

#ifndef LED_LOGGER_H
#define LED_LOGGER_H

#include <stddef.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

#include "driver/gpio.h"

////////////// DEFINES

#define LED_level_on 1
#define LED_level_off 0

#define LED_return_OK 0
#define LED_return_error -1
#define LED_return_not_initialized -2

////////////// GLOBALS

typedef struct {
	gpio_num_t pin;

	// 1 = active-high, 0 = active-low
	int active_high;

	// 1 after successful init
	int initialized;

	// Non-blocking blink state
	TimerHandle_t timer;

	// Uses static allocation (no heap)
	StaticTimer_t timer_storage;

	// 1 if currently ON, 0 if OFF
	int phase_on;

	 // Cycles remaining (counted on OFF edge)
	int remaining;
} LED_type;


// active_high: flip to 0 if you wired active-LOW
static LED_type LEDs[] = {
	// RED | Error
	{
		// D18
		.pin = GPIO_NUM_18,
		.active_high = 1,
		.initialized = 0
	},

	// ORANGNE | Warning
	{
		// D19
		.pin = GPIO_NUM_19,
		.active_high = 1,
		.initialized = 0
	},

	// GREEN | Success
	{
		// D23
		.pin = GPIO_NUM_23,
		.active_high = 1,
		.initialized = 0
	},
};

static const size_t LEDs_count = (sizeof(LEDs) / sizeof(LEDs[0]));

////////////// Forward declarations

static void LED_timer_callback(TimerHandle_t tmr);

////////////// Helpers

static inline int get_LED_level_on(const LED_type *LED) {
	return LED->active_high ? LED_level_on : LED_level_off;
}

static inline int get_LED_level_off(const LED_type *LED) {
	return LED->active_high ? LED_level_off : LED_level_on;
}

static inline int LED_init(LED_type *LED) {
	if (LED->initialized == 1) return LED_return_OK;

	if (!GPIO_IS_VALID_OUTPUT_GPIO(LED->pin)) return LED_return_error;

	// Glitch-free: reset -> preload OFF (still input) -> configure as output
	if (gpio_reset_pin(LED->pin) != ESP_OK) return LED_return_error;
	if (gpio_set_level(LED->pin, get_LED_level_off(LED)) != ESP_OK) return LED_return_error;

	gpio_config_t LED_IO_config = {
		.pin_bit_mask = 1ULL << LED->pin,
		.mode = GPIO_MODE_OUTPUT,
		.pull_up_en = GPIO_PULLUP_DISABLE,
		.pull_down_en = GPIO_PULLDOWN_DISABLE,
		.intr_type = GPIO_INTR_DISABLE
	};

	if (gpio_config(&LED_IO_config) != ESP_OK) return LED_return_error;

	LED->timer = xTimerCreateStatic(
		// name
		"LED",

		// dummy period; we set real period at start
		1,

		// auto-reload
		pdTRUE,

		// timer ID back-pointer
		(void*)LED,

		// Callback
		LED_timer_callback,

		// static storage
		&LED->timer_storage
	);

	if (LED->timer == NULL) return LED_return_error;

	LED->phase_on = 0;
	LED->remaining = 0;

	LED->initialized = 1;

	return LED_return_OK;
}

static inline int LED_on_RAW(LED_type *LED) {
	if (gpio_set_level(LED->pin, get_LED_level_on(LED)) != ESP_OK) return LED_return_error;
	return LED_return_OK;
}

static inline int LED_off_RAW(LED_type *LED) {
	if (gpio_set_level(LED->pin, get_LED_level_off(LED)) != ESP_OK) return LED_return_error;
	return LED_return_OK;
}

static inline int LED_off(LED_type *LED) {
	if (LED->timer) xTimerStop(LED->timer, 0);
    LED->remaining = 0;
    LED->phase_on = 0;

	if (LED_off_RAW(LED) != LED_return_OK) return LED_return_error;

	return LED_return_OK;
}

static void LED_timer_callback(TimerHandle_t tmr) {
	LED_type *LED = (LED_type *) pvTimerGetTimerID(tmr);

	if (!LED || !LED->initialized) return;

	if (LED->phase_on) {
		LED_off_RAW(LED);
		LED->phase_on = 0;

		if (LED->remaining > 0) {
			LED->remaining--;

			// stop blinking and stay OFF
			if (LED->remaining == 0) xTimerStop(LED->timer, 0);
		}

	}

	else {
		LED_on_RAW(LED);
		LED->phase_on = 1;
	}
}


// Blink: ON for interval_ms, then OFF for interval_ms, repeated `times` times.
// Returns 0 on success, -2 if not initialized, -1 on driver error.
static int LED_blink_start(LED_type *LED, uint32_t interval_ms, int times) {
	if (LED->initialized == 0) return LED_return_not_initialized;

	// Solid ON request
	if (interval_ms == 0) {
		// Stop any running timer
		if (LED->timer) xTimerStop(LED->timer, 0);
		LED->remaining = 0;
		LED->phase_on = 1;
		return LED_on_RAW(LED);
	}

	if (times == 0) return LED_return_OK;

	// Convert to ticks; make sure it's at least 1 tick so we actually delay.
    TickType_t d = pdMS_TO_TICKS(interval_ms);
    if (d == 0) d = 1;

	// Prepare state: turn ON immediately, then timer will toggle every 'd'
	if (LED_on_RAW(LED) != LED_return_OK) return LED_return_error;
	LED->phase_on = 1;
	LED->remaining = times;

	if (LED->timer == NULL) return LED_return_error;

	// Set period and (re)start
	if (xTimerChangePeriod(LED->timer, d, 0) != pdPASS) return LED_return_error;
	if (xTimerStart(LED->timer, 0) != pdPASS) return LED_return_error;

	return LED_return_OK;
}


////////////// APIs

static int LEDs_init(void) {
	for (size_t i = 0; i < LEDs_count; ++i) {
		int return_value = LED_init(&LEDs[i]);
		if (return_value != LED_return_OK) return LED_return_error;
	}

	return LED_return_OK;
}

static int LED_RED_on(int interval_ms, int times) { return LED_blink_start(&LEDs[0], (uint32_t)interval_ms, times); }
static int LED_RED_off() { return LED_off(&LEDs[0]); }

static int LED_ORANGE_on(int interval_ms, int times) { return LED_blink_start(&LEDs[1], (uint32_t)interval_ms, times); }
static int LED_ORANGE_off() { return LED_off(&LEDs[1]); }

static int LED_GREEN_on(int interval_ms, int times) { return LED_blink_start(&LEDs[2], (uint32_t)interval_ms, times); }
static int LED_GREEN_off() { return LED_off(&LEDs[2]); }

#endif
