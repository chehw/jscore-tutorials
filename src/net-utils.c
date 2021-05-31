/*
 * net-utils.c
 * 
 * Copyright 2021 chehw <hongwei.che@gmail.com>
 * 
 * The MIT License
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy of 
 * this software and associated documentation files (the "Software"), to deal in 
 * the Software without restriction, including without limitation the rights to 
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies 
 * of the Software, and to permit persons to whom the Software is furnished to 
 * do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all 
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE 
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, 
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE 
 * SOFTWARE.
 * 
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "net-utils.h"

#define HTTP_HEADERS_ALLOC_SIZE (64)
static int http_headers_resize(struct net_utils_http_headers * headers, size_t new_size)
{
	if(new_size == 0) new_size = HTTP_HEADERS_ALLOC_SIZE;
	else new_size = (new_size + HTTP_HEADERS_ALLOC_SIZE - 1) / HTTP_HEADERS_ALLOC_SIZE * HTTP_HEADERS_ALLOC_SIZE;

	if(new_size <= headers->size) return 0;
	
	skey_value_pair_t ** list = realloc(headers->list, sizeof(*list) * new_size);
	assert(list);
	memset(list + headers->size, 0, (new_size - headers->size) * sizeof(*list));
	
	headers->size = new_size;
	headers->list = list;
	return 0;
}

static int http_headers_add(struct net_utils_http_headers * headers, const char * key, const char * value)
{
	int rc = http_headers_resize(headers, headers->length + 1);
	assert(0 == rc);
	
	skey_value_pair_t * pair = skey_value_pair_new(key, value, -1);
	assert(pair);
	headers->list[headers->length++] = pair;
	return 0;
}

static int http_headers_add_line(struct net_utils_http_headers * headers, const char * line, ssize_t cb_line)
{
	static const char * delim = ":\r\n";
	if(NULL == line) return -1;
	
	char line_buf[PATH_MAX] = "";
	
	if(cb_line == -1) cb_line = strlen(line);
	if(cb_line <= 0 || cb_line >= sizeof(line_buf)) return -1;
	
	memcpy(line_buf, line, cb_line);
	line_buf[cb_line] = '\0';
	
	char * tok = NULL;
	char * value = NULL;
	char * key = strtok_r(line_buf, delim, &tok);
	assert(key);
	value = strtok_r(NULL, delim, &tok);
	
	return http_headers_add(headers, key, value);
}

struct net_utils_http_headers * net_utils_http_headers_init(struct net_utils_http_headers * headers, size_t size)
{
	headers->add = http_headers_add;
	headers->add_line = http_headers_add_line;
	return headers;
}

void net_utils_http_headers_cleanup(struct net_utils_http_headers * headers)
{
	if(NULL == headers) return;
	if(headers->list) {
		for(size_t i = 0; i < headers->length; ++i) {
			skey_value_pair_free(headers->list[i]);
		}
		free(headers->list);
		headers->list = NULL;
	}
	headers->length = 0;
	headers->size = 0;
	return;
}



static int set_url(struct net_utils_http_client * client, const char * url)
{
	assert(client && client->curl);
	assert(url && url[0]);
	strncpy(client->url, url, sizeof(client->url));
	return 0;
}
static int set_option(struct net_utils_http_client * client, CURLoption option, void * option_value)
{
	assert(client && client->curl);
	CURLcode ret = curl_easy_setopt(client->curl, option, option_value);
	return ret;
}

static int send_request(struct net_utils_http_client * client, const char * method, const void * payload, size_t cb_payload)
{
	assert(client && client->curl);
	int rc = -1;
	CURL * curl = client->curl;
	CURLcode ret = CURLE_OK;
	struct curl_slist * headers_list = NULL;
	
	auto_buffer_t * out_buf = client->out_buf;
	if(NULL == method) method = "GET";

	// step 0. clear input buffers (clear caches to accept new responses from the server)
	curl_easy_reset(curl);
	client->last_error = NULL;
	if(client->status_line) free(client->status_line);
	client->status_line = NULL;
	client->protocol = NULL;
	client->status_code = NULL;
	client->status_descriptions	= NULL;
	
	client->err_code = 0;
	client->response_code = 0;
	client->in_buf->length = 0;
	client->in_buf->start_pos = 0;
	net_utils_http_headers_cleanup(client->response_headers);

	// step 1. set url
	ret = curl_easy_setopt(curl, CURLOPT_URL, client->url);
	if(ret == CURLE_OK && client->use_ssl) {
		ret = curl_easy_setopt(curl, CURLOPT_USE_SSL, (long)client->use_ssl);
	}
	
	// step 2. set parse header and respose callbacks
	if(ret == CURLE_OK) {
		ret = curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)client->in_buf);
		ret = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, client->on_response);
	}
	
	if(ret == CURLE_OK && client->on_parse_header) {
		ret = curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, client->on_parse_header);
		if(ret == CURLE_OK) ret = curl_easy_setopt(curl, CURLOPT_HEADERDATA, client);
	}
	
	// step 3. prepare post data
	if(payload && cb_payload) auto_buffer_push(out_buf, payload, cb_payload);
	if(out_buf->length > 0) {
		ret = curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
		if(ret == CURLE_OK) ret = curl_easy_setopt(curl, CURLOPT_READFUNCTION, client->on_post_data);
		if(ret == CURLE_OK) ret = curl_easy_setopt(curl, CURLOPT_READDATA, client->out_buf);
	}
	if(ret != CURLE_OK) goto label_final;
	
	// step 4. set the default options according to the request method
	ret = curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
	if(ret != CURLE_OK) goto label_final;
	
	if(strcasecmp(method, "GET") == 0) {
		ret = curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
	}else if(strcasecmp(method, "HEAD") == 0) {
		ret = curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
	}
	else if(strcasecmp(method, "POST") == 0) {
		ret = curl_easy_setopt(curl, CURLOPT_HTTPPOST, 1L);
	}
	else if(strcasecmp(method, "PUT") == 0)
	{
		ret = curl_easy_setopt(curl, CURLOPT_PUT, 1L);
	}else {
		// ...
		fprintf(stderr, "[INFO]: method=%s\n", method);
	}
	
	// step 5. set http request headers
	struct net_utils_http_headers * req_hdrs = client->request_headers;
	char line[PATH_MAX] = "";
	int cb = 0;
	if(req_hdrs->length > 0) {
		for(int i = 0; i < req_hdrs->length; ++i) {
			const char * key = req_hdrs->list[i]->key;
			const char * value = req_hdrs->list[i]->value;
			assert(key && key[0]);
			cb = snprintf(line, sizeof(line), "%s: %s", key, value);
			if(cb > 0) {
				headers_list = curl_slist_append(headers_list, line);
			}
		}
	}
	if(out_buf->length > 0) {
		cb = snprintf(line, sizeof(line), "Content-Length: %ld", (long)out_buf->length);
		headers_list = curl_slist_append(headers_list, line);
	}
	if(headers_list) {
		ret = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers_list);
	}
	if(ret != CURLE_OK) goto label_final;
	
	// step 6. send request
	ret = curl_easy_perform(curl);
	if(ret == CURLE_OK) {
		ret = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &client->response_code);
		if(ret == CURLE_OK) rc = 0;
	}
	
label_final:
	if(headers_list) curl_slist_free_all(headers_list);
	client->err_code = ret;
	client->last_error = curl_easy_strerror(ret);
	return rc;
}
static void reset(struct net_utils_http_client * client)
{
	if(NULL == client) return;
	CURL * curl = client->curl;
	if(curl) curl_easy_reset(curl);
	
	client->err_code = 0;
	client->response_code = 0;
	
	net_utils_http_headers_cleanup(client->request_headers);
	net_utils_http_headers_cleanup(client->response_headers);
	
	client->in_buf->length = 0;
	client->in_buf->start_pos = 0;
	
	client->out_buf->length = 0;
	client->out_buf->start_pos = 0;
	
	client->last_error = NULL;
	if(client->status_line) free(client->status_line);
	client->status_line = NULL;
	client->protocol = NULL;
	client->status_code = NULL;
	client->status_descriptions	= NULL;
	return;
}

// custom callbacks
static size_t on_parse_header(char * ptr, size_t size, size_t n, void * user_data)
{
	assert(user_data);
	size_t cb = size * n;
	if(cb <= 2) return cb;
	if(user_data == stdout || user_data == stderr) {
		fprintf((FILE *)user_data, "%*s\n", (int)cb, ptr);
		return cb;
	}
	
	struct net_utils_http_client * client = user_data;
	struct net_utils_http_headers * hdrs = client->response_headers;
	
	if(NULL == client->status_line) {	// first line of the http request
		char status_line[PATH_MAX] = "";
		if(cb >= sizeof(status_line)) return 0;
		
		memcpy(status_line, ptr, cb);
		status_line[cb] = '\0';
		
		client->status_line = strdup(status_line);
		static const char * delim = " \r\n";
		char * token = NULL;
		
		client->protocol = strtok_r(client->status_line, delim, &token);
		if(NULL == client->protocol) return 0;
		
		client->status_code = strtok_r(NULL, delim, &token);
		if(client->status_code) client->status_descriptions = strtok_r(NULL, delim, &token);
		return cb;
	}
	
	
	
	int rc = hdrs->add_line(hdrs, ptr, cb);
	if(0 == rc) return cb;
	return 0;
}

static size_t on_response(char * ptr, size_t size, size_t n, void * user_data)
{
	assert(user_data);
	size_t cb = size * n;
	if(cb == 0) return 0;
	if(user_data == stdout || user_data == stderr) {
		fprintf((FILE *)user_data, "%*s", (int)cb, ptr);
	}
	
	auto_buffer_t * buf = user_data;
	int rc = auto_buffer_push(buf, ptr, cb);
	if(0 == rc) return cb;
	return 0;
}
static size_t on_post_data(char * ptr, size_t size, size_t n, void * user_data)
{
	assert(user_data);
	auto_buffer_t * buf = user_data;
	if(buf->length == 0) return 0;
	
	size_t buf_size = size * n;
	if(buf_size == 0) return 0;
	
	ssize_t cb = auto_buffer_pop(buf, (unsigned char **)&ptr, buf_size);
	return cb;
}

struct net_utils_http_client * net_utils_http_client_init(struct net_utils_http_client * client, void * user_data)
{
	if(NULL == client) {
		client = calloc(1, sizeof(*client));
		assert(client);
	}
	CURL * curl = curl_easy_init();
	assert(curl);
	
	client->curl = curl;
	
	net_utils_http_headers_init(client->request_headers, 0);
	net_utils_http_headers_init(client->response_headers, 0);
	
	auto_buffer_init(client->in_buf, 0);
	auto_buffer_init(client->out_buf, 0);
	
	client->set_url = set_url;
	client->set_option = set_option;
	client->send_request = send_request;
	client->reset = reset;
	
	client->on_parse_header = on_parse_header;
	client->on_response = on_response;
	client->on_post_data = on_post_data;
	
	return client;
}

void net_utils_http_client_cleanup(struct net_utils_http_client * client)
{
	if(NULL == client) return;
	
	net_utils_http_headers_cleanup(client->request_headers);
	net_utils_http_headers_cleanup(client->response_headers);
	
	auto_buffer_cleanup(client->in_buf);
	auto_buffer_cleanup(client->out_buf);
	
	if(client->status_line) {
		free(client->status_line);
		client->status_line = NULL;
	}
	
	if(client->curl) {
		curl_easy_cleanup(client->curl);
		client->curl = NULL;
	}
	return;
}
