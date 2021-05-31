/*
 * email-sender.c
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
#include <stdarg.h>

#include <pthread.h>
#include <uuid/uuid.h>
#include <curl/curl.h>
#include "email-sender.h"
#include "auto_buffer.h"

#ifndef debug_printf
#include <stdarg.h>

#ifdef _DEBUG
#define debug_printf fprintf
#else
#define debug_printf(...) do {} while(0)
#endif
#endif

/***************************************
 * struct email_private
***************************************/

typedef struct email_private 
{
	struct email_sender_context * email;
	CURL * curl;
	
	int err_code;
	char err_msg[PATH_MAX];
	
	// payload
	auto_buffer_t payload[1];
	
} email_private_t;
static email_private_t * email_private_new(struct email_sender_context * email)
{
	assert(email);
	
	email_private_t * priv = calloc(1, sizeof(*priv));
	assert(priv);
	priv->email = email;
	email->priv = priv;
	
	CURL * curl = curl_easy_init();
	assert(curl);
	priv->curl = curl;
	
	auto_buffer_init(priv->payload, 0);
	return priv;
}

static void email_private_clear(email_private_t * priv)
{
	if(NULL == priv) return;
	priv->payload->length = 0;
	priv->payload->start_pos = 0;
	
	if(priv->curl) curl_easy_reset(priv->curl);
	return;
}

static void email_private_free(email_private_t * priv)
{
	if(NULL == priv) return;
	email_private_clear(priv);
	auto_buffer_cleanup(priv->payload);
	if(priv->curl) {
		curl_easy_cleanup(priv->curl);
		priv->curl = NULL;
	}
	free(priv);
	return;
}


/***************************************
 * struct email_sender_context
***************************************/

static size_t on_read_data(char * ptr, size_t size, size_t n, void * user_data)
{
	size_t cb = size * n;
	if(cb == 0) return 0;
	
	auto_buffer_t * payload = user_data;
	assert(payload);
	if(payload->length == 0) return 0;
	
	if(cb > payload->length) cb = payload->length;
	memcpy(ptr, payload->data + payload->start_pos, cb);
	payload->start_pos += cb;
	payload->length -= cb; 
	
	return cb;
}

static int email_libcurl_send(struct email_sender_context * email)
{
	assert(email && email->priv);
	email_private_t * priv = email->priv;
	CURL * curl = priv->curl;
	if(NULL == curl) {
		curl = curl_easy_init();
		assert(curl);
		priv->curl = curl;
	}else {
		curl_easy_reset(curl);
	}
	
	long use_ssl = CURLUSESSL_ALL;
	if(email->security_mode == smtp_security_mode_try_tls) use_ssl = CURLUSESSL_TRY;
	
	// clear last error
	priv->err_code = 0;
	priv->err_msg[0] = '\0';
	
	CURLcode ret = CURLE_UNKNOWN_OPTION;
	ret = curl_easy_setopt(curl, CURLOPT_URL, email->url);
	ret = curl_easy_setopt(curl, CURLOPT_USERNAME, email->username);
	ret = curl_easy_setopt(curl, CURLOPT_PASSWORD, email->password);
	ret = curl_easy_setopt(curl, CURLOPT_USE_SSL, (long)use_ssl);
	
	struct email_address_list * addr_list = email->addr_list;
	if(addr_list->mail_from_addr->addr && addr_list->mail_from_addr->cb_addr > 0) {
		ret = curl_easy_setopt(curl, CURLOPT_MAIL_FROM, addr_list->mail_from_addr->addr);
	}
	struct curl_slist * recipients = NULL;
	for(int i = 0; i < addr_list->num_recipients; ++i) {
		struct email_address_data * recipient = addr_list->recipients_addrs[i];
		if(NULL == recipient || NULL == recipient->addr || recipient->addr <= 0) continue;
		
		recipients = curl_slist_append(recipients, recipient->addr);
	}
	ret = curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);
	
	/*
	 * prepare email payload and serialized to priv->payload, 
	 * since the offset need to be modified during the curl library processing
	 */
	ret = email->prepare_payload(email, 0, priv->payload, NULL);
	assert(0 == ret);
	
	ret = curl_easy_setopt(curl, CURLOPT_READFUNCTION, on_read_data);
	ret = curl_easy_setopt(curl, CURLOPT_READDATA, priv->payload);
	ret = curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);

//~ #ifdef CURL_VERBOSE
	ret = curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
//~ #endif

	ret = curl_easy_perform(curl);
	if(ret != CURLE_OK) {
		fprintf(stderr, "%s() failed: ret=%d, err_msg=%s\n",
			__FUNCTION__, 
			(int)ret,
			curl_easy_strerror(ret));
	}
	priv->err_code = ret;
	curl_slist_free_all(recipients);

	return priv->err_code;
}


static void email_libcurl_cleanup(struct email_sender_context * email)
{
	if(NULL == email) return;
	email_private_free(email->priv);
	email->priv = NULL;
	return;
}


static pthread_once_t s_once_key = PTHREAD_ONCE_INIT;
static void init_dependencies()
{
	curl_global_init(CURL_GLOBAL_ALL);
}
struct email_sender_context * email_sender_libcurl_init(struct email_sender_context * email, void * user_data)
{
	pthread_once(&s_once_key, init_dependencies);
	
	assert(email);
	
	email->send = email_libcurl_send;
	email->cleanup = email_libcurl_cleanup;
	
	email_private_t * priv = email_private_new(email);
	assert(priv && email->priv == priv);
	
	return email;
}


