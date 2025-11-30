#ifndef woXrooX_MIC_H
#define woXrooX_MIC_H

/////// I²S
// Wiring (INMP441):
// VDD → 3v3
// GND → GND
// SCK (BCLK) → D33 (GPIO33)
// WS (LRCLK) → D25 (GPIO25)
// SD (data out) → D32 (GPIO32)
// L/R → GND (mic = Left channel)

/*
Usage:

MIC_listen_start();

QueueHandle_t que = MIC_listen_queue();
MIC_frame_type frame;

for (;;) {
	if (xQueueReceive(que, &frame, portMAX_DELAY) == pdTRUE) {
		// frame.seq, frame.ts_us, frame.pcm[320] → send to your WebSocket streamer / VAD / STT
	}
}
*/



#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_timer.h"
#include "esp_log.h"

#include "driver/i2s_std.h"

////////////// DEFINES

// D33
#define PIN_BCLK 33

// D25
#define PIN_LRCK 25

// D32
#define PIN_DIN 32

#define SAMPLE_RATE 16000

// DMA frame length in 32-bit words
#define FRAME_WORDS 256

// number of DMA descriptors
#define DMA_FRAMES 6

// STT framing: 20 ms = 320 samples @ 16 kHz
#define STT_FRAME_SAMPLES 320

// Right-shift from 24-bit-left-justified to 16-bit PCM (tune 8..12)
#define SHIFT_BITS 11

// Queue capacity (frames). 64 ≈ 1.28 s at 20 ms/frame
#define MIC_QUEUE_LEN 64

////////////// TYPES

typedef struct {
	uint32_t seq;
	uint64_t ts_us;
	int16_t  pcm[STT_FRAME_SAMPLES];
} MIC_frame_type;

////////////// GLOBALS

static const char *MIC_TAG = "woXrooX::MIC:";

// Read buffer: 32-bit words. Mic gives 24 valid bits left-justified in 32.
static int32_t MIC_buffer[1024];

static i2s_chan_handle_t RX_channel;

// Frame assembly (carry remainder across I2S reads)
static int16_t frame_accum[STT_FRAME_SAMPLES];
static size_t  frame_fill = 0;
static uint64_t frame_ts_us = 0;
static uint32_t frame_seq = 0;

static QueueHandle_t MIC_queue = NULL;

////////////// I2S

static void init_i2s(void) {
	// Create RX channel
	i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(0, I2S_ROLE_MASTER);
	chan_cfg.dma_desc_num  = DMA_FRAMES;
	chan_cfg.dma_frame_num = FRAME_WORDS;

	ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &RX_channel));

	// Clock config (APLL = cleaner clock)
	i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE);
	clk_cfg.clk_src = I2S_CLK_SRC_APLL;

	// Slot config: Philips I2S, 32-bit slots, mono LEFT
	i2s_std_slot_config_t slot_cfg = {
		.data_bit_width = I2S_DATA_BIT_WIDTH_32BIT,
		.slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT,
		.slot_mode = I2S_SLOT_MODE_MONO,
		.slot_mask = I2S_STD_SLOT_LEFT,

		// auto: 1 BCLK for Philips
		.ws_width = 0,

		// standard WS polarity
		.ws_pol = false,

		// Philips format (data delayed by 1 BCLK)
		.bit_shift = true
	};

	// GPIO config
	i2s_std_gpio_config_t gpio_cfg = {
		.mclk = I2S_GPIO_UNUSED,
		.bclk = PIN_BCLK,
		.ws = PIN_LRCK,
		.dout = I2S_GPIO_UNUSED,
		.din = PIN_DIN,

		.invert_flags = {
			.mclk_inv = false,
			.bclk_inv = false,
			.ws_inv = false
		}
	};

	// STD config
	i2s_std_config_t std_cfg = {
		.clk_cfg = clk_cfg,
		.slot_cfg = slot_cfg,
		.gpio_cfg = gpio_cfg
	};

	ESP_ERROR_CHECK(i2s_channel_init_std_mode(RX_channel, &std_cfg));
	ESP_ERROR_CHECK(i2s_channel_enable(RX_channel));
}

////////////// TASK: read I2S → make 320-sample frames → enqueue

static void mic_rx_task(void *param) {
	(void)param;

	while (1) {
		size_t nbytes = 0;
		esp_err_t err = i2s_channel_read(RX_channel, MIC_buffer, sizeof(MIC_buffer), &nbytes, portMAX_DELAY);
		if (err != ESP_OK || nbytes == 0) continue;

		const size_t n = nbytes / sizeof(int32_t);

		for (size_t i = 0; i < n; ++i) {
			int16_t s16 = (int16_t)(MIC_buffer[i] >> SHIFT_BITS);

			// timestamp at the first sample of an empty frame
			if (frame_fill == 0) frame_ts_us = esp_timer_get_time();

			frame_accum[frame_fill++] = s16;

			if (frame_fill == STT_FRAME_SAMPLES) {
				MIC_frame_type frame;
				frame.seq   = ++frame_seq;
				frame.ts_us = frame_ts_us;
				memcpy(frame.pcm, frame_accum, sizeof(frame.pcm));

				if (MIC_queue) {
					// If full, drop the oldest to keep latency bounded
					if (xQueueSend(MIC_queue, &frame, 0) != pdTRUE) {
						MIC_frame_type dump;
						xQueueReceive(MIC_queue, &dump, 0);
						xQueueSend(MIC_queue, &frame, 0);
					}
				}

				frame_fill = 0;
			}
		}
	}
}

////////////// API

// Call once at startup to begin capturing and enqueuing frames.
static void MIC_listen_start(void) {
	if (!MIC_queue) MIC_queue = xQueueCreate(MIC_QUEUE_LEN, sizeof(MIC_frame_type));

	init_i2s();
	xTaskCreatePinnedToCore(mic_rx_task, "MIC_RX", 4096, NULL, 5, NULL, tskNO_AFFINITY);
}

// Getter for your STT task: pop frames with xQueueReceive()
static QueueHandle_t MIC_listen_queue(void) {
	return MIC_queue;
}

#endif
