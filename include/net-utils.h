#ifndef NET_UTILS_H_
#define NET_UTILS_H_

#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif

#include <limits.h>
#include <curl/curl.h>

#include "auto_buffer.h"
#include "skey_value_pair.h"

struct net_utils_http_headers
{
	size_t size;
	size_t length;
	skey_value_pair_t ** list;
	
	int (* add_line)(struct net_utils_http_headers * headers, const char * line, ssize_t cb_line);
	int (* add)(struct net_utils_http_headers * headers, const char * key, const char * value);
};
struct net_utils_http_headers * net_utils_http_headers_init(struct net_utils_http_headers * headers, size_t size);
void net_utils_http_headers_cleanup(struct net_utils_http_headers * headers);

struct net_utils_http_client
{
	CURL * curl;
	void * priv;
	void * user_data;
	
	// data
	char url[PATH_MAX];
	int use_ssl;
	int verify_host;
	char * status_line;
	const char * protocol;
	const char * status_code;
	const char * status_descriptions;
	
	struct net_utils_http_headers request_headers[1];
	struct net_utils_http_headers response_headers[1];
	auto_buffer_t in_buf[1];
	auto_buffer_t out_buf[1];
	CURLcode err_code;
	long response_code;
	const char * last_error;
	
	// public methods
	int (* set_url)(struct net_utils_http_client * client, const char * url);
	int (* set_option)(struct net_utils_http_client * client, CURLoption option, void * option_value);
	int (* send_request)(struct net_utils_http_client * client, const char * method, const void * payload, size_t cb_payload);
	void (* reset)(struct net_utils_http_client * client);
	
	// custom callbacks
	size_t (* on_parse_header)(char * ptr, size_t size, size_t n, void * user_data);
	size_t (* on_response)(char * ptr, size_t size, size_t n, void * user_data);
	size_t (* on_post_data)(char * ptr, size_t size, size_t n, void * user_data);
};

struct net_utils_http_client * net_utils_http_client_init(struct net_utils_http_client * client, void * user_data);
void net_utils_http_client_cleanup(struct net_utils_http_client * client);


#ifdef __cplusplus
}
#endif
#endif

