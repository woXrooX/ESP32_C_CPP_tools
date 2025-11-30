#ifndef woXrooX_HTTP_CLIENT_H
#define woXrooX_HTTP_CLIENT_H

/*
Usage:
	char *body = NULL;
	int status = 0;

	// GET
	if (HTTP_GET("http://127.0.0.1:8000/", &body, &status) == 0) {
		ESP_LOGI("woXrooX::HTTP_CLIENT", "status=%d\n%s", status, body);
		free(body);
	}

	// POST JSON
	if (HTTP_POST_JSON("http://127.0.0.1:8000/api", "{\"for\":\"woXrooX.com\"}", &body, &status) == 0) {
		ESP_LOGI("woXrooX::HTTP_CLIENT", "status=%d\n%s", status, body);
		free(body);
	}

Return values:
0 = success
-1 = invalid arg
-2 = no memory
-3 = client init failed
-4 = open/connect failed
-5 = write failed
-6 = read failed

HTTPS:
- Use https:// URLs and enable the cert bundle in menuconfig:
  Component config → mbedTLS → Certificate Bundle → Enable trusted root certificates bundle
- Then uncomment `.crt_bundle_attach = esp_crt_bundle_attach` below.
*/

#include <stdlib.h>
#include <string.h>

#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"

static const char *HTTP_CLIENT_TAG = "woXrooX::HTTP_client";

static int HTTP_read_all(esp_http_client_handle_t client, char **out_body, size_t *out_length) {
	if (!out_body) return -1;

	size_t cap = 1024;
	size_t length = 0;
	char *buffer = (char *)malloc(cap);

	if (!buffer) return -2;

	for (;;) {
		if (length + 1 >= cap) {
			size_t new_cap = cap * 2;
			char *tmp = (char *)realloc(buffer, new_cap);
			if (!tmp) { free(buffer); return -2; }
			buffer = tmp;
			cap = new_cap;
		}

		int n = esp_http_client_read(client, buffer + length, cap - length - 1);

		if (n < 0) { free(buffer); return -6; }
		if (n == 0) break;
		length += (size_t)n;
	}

	buffer[length] = '\0';
	*out_body = buffer;
	if (out_length) *out_length = length;

	return 0;
}

static int HTTP_GET(
	const char *URL,
	char **out_body,
	int *out_status_code
) {
	if (!URL || !out_body) return -1;

	esp_http_client_config_t configuration = {
		.url = URL,
		.timeout_ms = 10000,
		// .crt_bundle_attach = esp_crt_bundle_attach, // enable for HTTPS
	};
	esp_http_client_handle_t client = esp_http_client_init(&configuration);
	if (!client) return -3;

	esp_http_client_set_header(client, "Connection", "close");

	esp_err_t err = esp_http_client_open(client, 0);
	if (err != ESP_OK) {
		esp_http_client_cleanup(client);
		return -4;
	}

	(void)esp_http_client_fetch_headers(client);
	if (out_status_code) *out_status_code = esp_http_client_get_status_code(client);

	char *body = NULL;
	int response = HTTP_read_all(client, &body, NULL);

	esp_http_client_close(client);
	esp_http_client_cleanup(client);

	if (response != 0) return response;
	*out_body = body;

	return 0;
}

static int HTTP_POST_JSON(
	const char *URL,
	const char *JSON_body,
	char **out_body,
	int *out_status_code
) {
	if (!URL || !JSON_body || !out_body) return -1;

	esp_http_client_config_t configuration = {
		.url = URL,
		.timeout_ms = 10000,
		// .crt_bundle_attach = esp_crt_bundle_attach, // enable for HTTPS
	};
	esp_http_client_handle_t client = esp_http_client_init(&configuration);
	if (!client) return -3;

	esp_http_client_set_method(client, HTTP_METHOD_POST);
	esp_http_client_set_header(client, "Content-Type", "application/json");
	esp_http_client_set_header(client, "Connection", "close");

	size_t body_length = strlen(JSON_body);
	esp_err_t err = esp_http_client_open(client, body_length);
	if (err != ESP_OK) {
		esp_http_client_cleanup(client);
		return -4;
	}

	int written = esp_http_client_write(client, JSON_body, body_length);
	if (written < 0 || (size_t)written != body_length) {
		esp_http_client_close(client);
		esp_http_client_cleanup(client);
		return -5;
	}

	(void)esp_http_client_fetch_headers(client);
	if (out_status_code) *out_status_code = esp_http_client_get_status_code(client);

	char *body = NULL;
	int response = HTTP_read_all(client, &body, NULL);

	esp_http_client_close(client);
	esp_http_client_cleanup(client);

	if (response != 0) return response;
	*out_body = body;

	return 0;
}

#endif
