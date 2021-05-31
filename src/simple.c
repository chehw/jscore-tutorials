/*
 * simple.c
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

#include <jsc/jsc.h>

#include <webkit2/webkit2.h>
#include "utils.h"
#include "js-utils.h"
#include "net-utils.h"

typedef int (* js_utils_exception_callback)(JSCContext *, JSCException * exception, int exit_app, JSCValue * ret_val);
static int js_utils_check_result(JSCContext * js, JSCValue * ret_val, int exit_app, js_utils_exception_callback on_exception) 
{
	JSCException * exception = jsc_context_get_exception(js);
	if(NULL != exception) {
		if(on_exception) return on_exception(js, exception, exit_app, ret_val);
		fprintf(stderr, "Exception: (from: %s@%d), %s:%s\n",
			jsc_exception_get_source_uri(exception), jsc_exception_get_line_number(exception),
			jsc_exception_get_name(exception),
			jsc_exception_get_message(exception)
		);
		g_object_unref(exception);
		if(exit_app) exit(1);
	}
	return 0;
}

#define AUTO_CLEANUP_(struct_name) __attribute__((cleanup(struct_name##_cleanup))) struct struct_name
ssize_t load_js_code_from_uri(const char * uri, char ** p_js_code)
{
	char * js_code = NULL;
	ssize_t cb_code = 0;
	
	assert(uri);
	int rc = 0;
	int is_http = 0;
	int is_https = 0;
	
	is_http = (0 == strncasecmp(uri, "http://", sizeof("http://" - 1)));
	if(!is_http) is_https = (0 == strncasecmp(uri, "https://", sizeof("https://" - 1)));
	
	if(is_http || is_https) {
		AUTO_CLEANUP_(net_utils_http_client) http_client;
		memset(&http_client, 0, sizeof(http_client));
		
		struct net_utils_http_client * client = net_utils_http_client_init(&http_client, NULL);
		assert(client);
		
		rc = client->set_url(client, uri);
		if(rc) return -1;
		
		client->use_ssl = is_https;
		client->verify_host = is_https;
		rc = client->send_request(client, "GET", NULL, 0);
		if(rc) {
			fprintf(stderr, "[ERROR]: %s\n", client->last_error);
		}
		
		auto_buffer_t * in_buf = client->in_buf;
		cb_code = in_buf->length;
		if(cb_code > 0) {
			js_code = calloc(cb_code + 1, 1);
			assert(js_code);
			memcpy(js_code, in_buf->data + in_buf->start_pos, in_buf->length);
			js_code[cb_code] = '\0';
		}
		*p_js_code = js_code;
		return cb_code;
	}
	
	cb_code =  utils_load_file(NULL, uri, (unsigned char **)&js_code, NULL);
	*p_js_code = js_code;
	return cb_code;
}

int main(int argc, char **argv)
{
	int rc = 0;
	curl_global_init(CURL_GLOBAL_ALL);
	
	gtk_init(&argc, &argv);
	GtkWidget * webview = webkit_web_view_new();
	
	
	JSCContext * js = jsc_value_get_context(JSC_VALUE(webview));
	
	//~ const char * jquery_uri = "https://code.jquery.com/jquery-3.6.0.js";
	const char * jquery_uri = "jslib/jquery-3.6.0.js";
	const char * bootstrap_js_min_uri = "https://maxcdn.bootstrapcdn.com/bootstrap/3.3.7/js/bootstrap.min.js";
	char * js_code = NULL;
	ssize_t cb_code = 0;
	
	//~ JSCVirtualMachine * jsvm = jsc_virtual_machine_new();
	//~ JSCContext * js = jsc_context_new_with_virtual_machine(jsvm);
	JSCValue * ret_val = NULL;
	
	struct XMLDomClass {
		void * root;
	} document_object;
	JSCClass * dom_class = jsc_context_register_class(js, "XMLDomClass", NULL, NULL, NULL);
	JSCValue * document = jsc_value_new_object(js, &document_object, dom_class);
	rc = js_utils_check_result(js, document, 0, NULL);
	
	
	cb_code = load_js_code_from_uri(jquery_uri, &js_code);
	assert(js_code && cb_code > 0);
	ret_val = jsc_context_evaluate_with_source_uri(js, js_code, cb_code, jquery_uri, 1);
	rc = js_utils_check_result(js, ret_val, 0, NULL);
	assert(0 == rc);
	
	free(js_code); js_code = NULL;
	cb_code = load_js_code_from_uri(bootstrap_js_min_uri, &js_code);
	assert(js_code && cb_code > 0);
	
	ret_val = jsc_context_evaluate_with_source_uri(js, js_code, cb_code, bootstrap_js_min_uri, 1);
	rc = js_utils_check_result(js, ret_val, 0, NULL);
	assert(0 == rc);
	
	curl_global_cleanup();
	return 0;
}

