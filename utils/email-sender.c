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
#include <time.h>
#include <search.h>
#include "email-sender.h"

// user-agent plungins
struct email_sender_context * email_sender_libcurl_init(struct email_sender_context *, void *);

/* **************************************
 * struct email_addresses_list
** *************************************/
static const char * s_email_address_types_name[] = {
	[email_address_type_mail_from] = "From", 
	[email_address_type_to] = "To", 
	[email_address_type_cc] = "Cc",
	[email_address_type_bcc] = "Bcc",  
};
const char * email_address_type_to_string(enum email_address_type type)
{
	switch(type) {
	case email_address_type_mail_from:
	case email_address_type_to:
	case email_address_type_cc:
	case email_address_type_bcc:
		return s_email_address_types_name[(int)type];
	default:
		break;
	}
	return NULL;
}

struct email_address_data * email_address_data_set(
	struct email_address_data * email_addr, 
	enum email_address_type type, 
	const char * addr, int cb_addr)
{
	assert(addr && addr[0]);
	if(cb_addr == -1) cb_addr = strlen(addr);
	if(cb_addr == 0 || cb_addr > EMAIL_ADDRESS_MAX_LENGTH) return NULL;
	
	if(NULL == email_addr) {
		email_addr = calloc(1, sizeof(*email_addr));
		assert(email_addr);
	}else {
		memset(email_addr, 0, sizeof(*email_addr));
	}
	
	email_addr->type = type;
	email_addr->cb_addr = cb_addr;
	memcpy(email_addr->addr, addr, cb_addr);
	
	return email_addr;
}
void email_address_data_free(struct email_address_data * addr)
{
	free(addr);
}

#define ADDRESS_LIST_ALLOC_SIZE (128)
static int email_address_list_resize(struct email_address_list * list, size_t new_size)
{
	if(new_size == 0) new_size = ADDRESS_LIST_ALLOC_SIZE;
	else new_size = (new_size + ADDRESS_LIST_ALLOC_SIZE - 1) / ADDRESS_LIST_ALLOC_SIZE * ADDRESS_LIST_ALLOC_SIZE;
	
	if(new_size <= list->max_size) return 0;
	
	struct email_address_data ** recipients_addrs = realloc(list->recipients_addrs, sizeof(*recipients_addrs) * new_size);
	assert(recipients_addrs);
	
	memset(recipients_addrs + list->max_size, 0, sizeof(*recipients_addrs) * (new_size - list->max_size));
	list->recipients_addrs = recipients_addrs;
	list->max_size = new_size;
	
	return 0;
}
#undef ADDRESS_LIST_ALLOC_SIZE

static int email_address_list_add(struct email_address_list * list, enum email_address_type type, const char * addr)
{
	struct email_address_data * recipient = email_address_data_set(NULL, type, addr, -1);
	assert(recipient);
	
	void * p_node = tsearch(recipient, &list->addrs_root, (__compar_fn_t)strcmp);
	if(NULL == p_node) {	// should never happen, maybe caused by insufficient memory of the system
		perror("email_address_list_add::tsearch()");
		email_address_data_free(recipient);
		return -1;
	}
	
	struct email_address_data * current = *(void **)p_node;
	if(current != (void *)recipient) { // duplicate address found
		if(list->dup_policy == email_address_duplicates_replace_with_latest) {	// old records in the list can be replaced with the latest value
			*current = *recipient; // can be assigned directly (since no internal pointers within [struct email_address_data]) 
		}
		email_address_data_free(recipient);
		return 1;
	}

	int rc = email_address_list_resize(list, list->num_recipients + 1);
	assert(0 == rc);
	
	list->recipients_addrs[list->num_recipients++] = recipient;
	return 0;
}
static struct email_address_data * email_address_list_find(struct email_address_list * list, const char * addr)
{
	void * p_node = tfind(addr, &list->addrs_root, (__compar_fn_t)strcmp);
	if(p_node) return *(void **)p_node;
	return NULL;
}
static int email_address_list_remove(struct email_address_list * list, const char * addr)
{
	// TODO
	return -1;
}
static void email_address_list_clear(struct email_address_list * list)
{
	if(list->addrs_root) {
		tdestroy(list->addrs_root, (void (*)(void *))email_address_data_free);
		list->addrs_root = NULL;
	}
	free(list->recipients_addrs);
	list->recipients_addrs = NULL;
	list->num_recipients = 0;
	list->max_size = 0;
	return;
}

struct email_address_list * email_address_list_init(struct email_address_list * list, void * user_data)
{
	if(NULL == list) {
		list = calloc(1, sizeof(*list));
		assert(list);
	}
	list->user_data = user_data;
	list->add = email_address_list_add;
	list->find = email_address_list_find;
	list->remove = email_address_list_remove;
	list->clear = email_address_list_clear;
	
	int rc = email_address_list_resize(list, 0);
	assert(0 == rc);
	
	return list;
}

void email_address_list_cleanup(struct email_address_list * list)
{
	if(NULL == list) return;
	email_address_list_clear(list);
	return;
}

/* ********************************************
 * email_header
** *******************************************/
static int skey_value_pair_compare(const void * _a, const void * _b)
{
	const skey_value_pair_t * a = _a;
	const skey_value_pair_t * b = _b;
	assert(a->key && b->key);
	return strcmp(a->key, b->key);
}
static int  email_header_add(struct email_header * hdr, const char * key, const char * value)
{
	assert(hdr && key);
	ssize_t cb_value = 0;
	if(value) cb_value = strlen(value);
	
	skey_value_pair_t * kvp = skey_value_pair_new(key, value, cb_value);
	assert(kvp);
	
	void * p_node = avl_tree_add(hdr->root, kvp, skey_value_pair_compare);
	assert(p_node);
	
	skey_value_pair_t * current = *(void **)p_node;
	if( current != (void *)kvp) { // dup found, ALWAYS replace with the latest value
		skey_value_pair_replace_value(current, kvp->value, kvp->cb_value);	
		kvp->value = NULL;
		skey_value_pair_free(kvp);
	}else { // add new
		++hdr->num_items;
	}
	return 0;
}
static int  email_header_remove(struct email_header * hdr, const char * key)
{
	// TODO: 
	return -1;
}
static int  email_header_foreach(struct email_header * hdr, int (* callback)(const char * key, const char * value, void * user_data), void * user_data)
{
	assert(hdr && callback);
	struct avl_node * p_node = NULL;
	p_node = avl_tree_iter_begin(hdr->root);
	
	while(p_node) {
		skey_value_pair_t * kvp = *(void **)p_node;
		assert(kvp);
		callback(kvp->key, kvp->value, user_data);
		p_node = avl_tree_iter_next(hdr->root);
	}
	return 0;
}
static void email_header_clear(struct email_header * hdr)
{
	avl_tree_cleanup(hdr->root);
	hdr->num_items = 0;
}

struct email_header * email_header_init(struct email_header * hdr, void * user_data)
{
	if(NULL == hdr) {
		hdr = calloc(1, sizeof(*hdr));
		assert(hdr);
	}
	
	hdr->user_data = user_data;
	hdr->add = email_header_add;
	hdr->remove = email_header_remove;
	hdr->foreach = email_header_foreach;
	hdr->clear = email_header_clear;
	
	avl_tree_t * tree = avl_tree_init(hdr->root, hdr);
	assert(tree);
	
	tree->on_free_data = (void (*)(void *))skey_value_pair_free;
	return hdr;
}
void email_header_cleanup(struct email_header * hdr)
{
	if(NULL == hdr) return;
	if(hdr->clear) hdr->clear(hdr);
	return;
}

/* ********************************************
 * email_sender 
** *******************************************/
static int email_set_smtp_server(struct email_sender_context * email, enum smtp_security_mode mode, const char * server_name, unsigned int port)
{
	assert(email);
	static const char * s_protocols[] = {
		"smtp",
		"smtps",
	};
	email->security_mode = mode;
	const char * protocol = s_protocols[(mode == smtp_security_mode_ssl)];
	if(0 == port) {
		switch(mode)
		{
		case smtp_security_mode_ssl: 
			port = 465; break;
		default:
			port = 587; 	// default mail submission port
		}
	}
	int cb = snprintf(email->url, sizeof(email->url), "%s://%s:%u", protocol, server_name, port);
	assert(cb > 0);
	
	return 0;
}
static int email_set_auth_plain(struct email_sender_context * email, const char * username, const char * password)
{
	assert(email);
	if(username) strncpy(email->username, username, sizeof(email->username));
	if(password) strncpy(email->password, password, sizeof(email->password));
	return 0;
}

static int email_set_from_addr(struct email_sender_context * email, const char * from_addr)
{
	assert(email);
	struct email_address_list * addr_list = email->addr_list;
	
	struct email_address_data * mail_from = email_address_data_set(addr_list->mail_from_addr, 
		email_address_type_mail_from, 
		from_addr, -1);
		
	assert(mail_from);
	return 0;
}

static int email_add_recipients(struct email_sender_context * email, enum email_address_type type, ...)
{
	assert(email);
	int rc = 0;
	int dups_found = 0;
	struct email_address_list * list = email->addr_list;
	const char * addr = NULL;
	va_list args;
	va_start(args, type);
	
	while((addr = va_arg(args, const char *))) {
		rc = list->add(list, type, addr);
		if(rc < 0) break;
		if(rc > 0) ++dups_found;
	}
	va_end(args);
	
	if(rc < 0) return rc;
	return dups_found;
}

static int email_add_body(struct email_sender_context * email, const char * text, size_t cb_text)
{
	assert(email);
	auto_buffer_t * body = email->body;
	
	if(NULL == text) return -1;
	if(cb_text == -1) cb_text = strlen(text);
	if(cb_text <= 0) return -1;
	
	return auto_buffer_push(body, text, cb_text);
}

static int email_add_header(struct email_sender_context * email, const char * key, const char * value)
{
	assert(email);
	struct email_header * hdr = email->hdr;
	
	return hdr->add(hdr, key, value);
}

static void email_clear(struct email_sender_context * email)
{
	assert(email);
	
	auto_buffer_cleanup(email->body);
	auto_buffer_cleanup(email->payload);
	email->prepared = 0;
	
	email->hdr->clear(email->hdr);
	email->addr_list->clear(email->addr_list);
	return;
}

static int append_key_value_pairs(const char * key, const char * value, void * user_data)
{
	int rc = 0;
	assert(key && user_data);
	auto_buffer_t * payload = user_data;
	
	int cb_key = strlen(key);
	int cb_value = 0;
	if(value) cb_value = strlen(value);
	
	rc = auto_buffer_push(payload, key, cb_key);
	rc = auto_buffer_push(payload, ": ", 2);
	if(cb_value > 0) {
		auto_buffer_push(payload, value, cb_value);
	}
	rc = auto_buffer_push(payload, "\r\n", 2);
	return rc;
}
static int email_prepare_payload(struct email_sender_context * email, 
	int escape_dot_char,
	auto_buffer_t * payload, const struct timespec * timestamp)
{
	assert(email && payload);
	int rc = 0;
	
	// reset auto_buffer
	payload->length = 0;
	payload->start_pos = 0;
	char line[PATH_MAX] = "";
	ssize_t cb_line = 0;
	
	// step 0: generate <to_addrs> and <cc_addrs> list
	struct email_address_list * addr_list = email->addr_list;
	char sz_addr[EMAIL_ADDRESS_MAX_LENGTH + 4] = "";
	int cb_addr = 0;
	
	auto_buffer_t to_addrs[1];
	auto_buffer_t cc_addrs[1];
	auto_buffer_init(to_addrs, 0);
	auto_buffer_init(cc_addrs, 0);
	//~ static const char * email_addr_fmt[] = {
		//~ "<%s> ",
		//~ "%s ",
	//~ };
	
	for(int i = 0; i < addr_list->num_recipients; ++i)
	{
		struct email_address_data * recipient = addr_list->recipients_addrs[i];
		if(NULL == recipient || NULL == recipient->addr) continue;
		switch(recipient->type)
		{
		case email_address_type_to:
		case email_address_type_cc:
			break;
		default:
			continue;	// do not show bcc recipients in email DATA
		}
		const char * fmt = "%s"; //email_addr_fmt[(recipient->addr[0] == '<')];
		cb_addr = snprintf(sz_addr, sizeof(sz_addr), fmt, recipient->addr);
		
		auto_buffer_t * dst_buf = (recipient->type == email_address_type_cc)?cc_addrs:to_addrs;
		if(dst_buf->length > 0) {
			auto_buffer_push(dst_buf, ", ", 2);
		}
		auto_buffer_push(dst_buf, sz_addr, cb_addr);
	}
	
	// Line 0: "Date: ...\r\n"
	char sz_date[100] = "";
	ssize_t cb_date = email_utils_generate_date(sz_date, sizeof(sz_date), timestamp);
	assert(cb_date > 0);
	
	cb_line = snprintf(line, sizeof(line), "Date: %s\r\n", sz_date);
	auto_buffer_push(payload, line, cb_line);
	
	// line 1: "From: <>"
	//~ const char * fmt = email_addr_fmt[(addr_list->mail_from_addr->addr[0] == '<')];
	const char * fmt = "%s";
	cb_addr = snprintf(sz_addr, sizeof(sz_addr), fmt, addr_list->mail_from_addr->addr);
	auto_buffer_push(payload, "From: ", sizeof("From: ") - 1);
	auto_buffer_push(payload, sz_addr, cb_addr);
	auto_buffer_push(payload, "\r\n", 2);
	
	// line 1: "To: <> <> ... <>\r\n"
	assert(to_addrs->length > 0);
	auto_buffer_push(payload, "To: ", sizeof("To: ") - 1);
	auto_buffer_push(payload, to_addrs->data, to_addrs->length);
	auto_buffer_push(payload, "\r\n", 2);
	
	// line 2: "Cc: <> <> ... <>\r\n"
	if(cc_addrs->length > 0) {
		auto_buffer_push(payload, "Cc: ", sizeof("Cc: ") - 1);
		auto_buffer_push(payload, to_addrs->data, to_addrs->length);
		auto_buffer_push(payload, "\r\n", 2);
	}
	
	// add other headers
	struct email_header * hdr = email->hdr;
	rc = hdr->foreach(hdr, append_key_value_pairs, payload);
	assert(0 == rc);
	
	// add an empty line to devide headers from body (rfc5322) 
	auto_buffer_push(payload, "\r\n", 2);
	
	// append body
	auto_buffer_t * body = email->body;
	if(body->length > 0) {
		const char * p = (const char *)body->data;
		const char * p_end = p + body->length;
			
		if(escape_dot_char) {
			char * dot_char = strchr(p, '.');
			while(p < p_end && dot_char) {
				auto_buffer_push(payload, p, dot_char - p);
				auto_buffer_push(payload, "..", 2);
				
				p = dot_char + 1;
				dot_char = strchr(p, '.');
			}
		}
		if(p < p_end) auto_buffer_push(payload, p, p_end - p);
	}
	
	auto_buffer_cleanup(to_addrs);
	auto_buffer_cleanup(cc_addrs);
	return 0;
}

struct email_sender_context * email_sender_context_init(struct email_sender_context * email, enum email_sender_user_agent agent, void * user_data)
{
	if(NULL == email) email = calloc(1, sizeof(*email));
	assert(email);
	
	email_address_list_init(email->addr_list, email);
	email_header_init(email->hdr, user_data);
	auto_buffer_init(email->body, 0);
	
	email->set_smtp_server = email_set_smtp_server;
	email->set_auth_plain = email_set_auth_plain;
	email->set_from_addr = email_set_from_addr;
	email->add_recipents = email_add_recipients;
	
	email->add_header = email_add_header;
	email->add_body = email_add_body;
	email->clear = email_clear;
	
	email->prepare_payload = email_prepare_payload;
	
	switch(agent) 
	{
	case email_sender_user_agent_default:
	case email_sender_user_agent_libcurl:
		return email_sender_libcurl_init(email, user_data);
	case email_sender_interactive:
		// TODO: 
		// return email_sender_interative_init(email, user_data);
	default:
		break;
	}
	return NULL;
}

void email_sender_context_cleanup(struct email_sender_context * email)
{
	if(NULL == email) return;
	if(email->cleanup) email->cleanup(email);
	
	email_header_cleanup(email->hdr);
	email_address_list_cleanup(email->addr_list);
	auto_buffer_cleanup(email->body);
	auto_buffer_cleanup(email->payload);
	email->prepared = 0;
	
	return;
}

/* 
 * generate rfc2822-compliant date string 
*/
ssize_t email_utils_generate_date(char sz_date[static 1], size_t size, const struct timespec * timestamp)
{
	// RFC 2822-compliant  date  format
	static const char * rfc_2882_date_fmt = "%a, %d %b %Y %T %z";
	
	assert(sz_date && size > 0);
	struct timespec ts[1];
	
	if(NULL == timestamp) {	// get current time
		memset(ts, 0, sizeof(ts));
		clock_gettime(CLOCK_REALTIME, ts);
		timestamp = ts;
	}
	
	struct tm t[1];
	memset(t, 0, sizeof(t));
	localtime_r(&timestamp->tv_sec, t);

	ssize_t cb_date = strftime(sz_date, size, rfc_2882_date_fmt, t);
	return cb_date;

}


void email_sender_context_dump(const struct email_sender_context * email)
{
	assert(email );
	fprintf(stderr, "url: %s\n", email->url);
	fprintf(stderr, "mode: %d\n", email->security_mode);
	fprintf(stderr, "username: %s\n", email->username);
	fprintf(stderr, "password: %s\n", email->password);
	
	const struct email_address_list * addr_list = email->addr_list;
	fprintf(stderr, "MAIL FROM %s\n", addr_list->mail_from_addr->addr);
	
	int num_recipients = addr_list->num_recipients;
	for(int i = 0; i < num_recipients; ++i) {
		const struct email_address_data * recipient = addr_list->recipients_addrs[i];
		assert(recipient);
		
		fprintf(stderr, "(%s) RCPT TO %s\n", 
			email_address_type_to_string(recipient->type),
			recipient->addr);
	}
	
	const auto_buffer_t * payload = email->payload;
	if(payload->length > 0) {
		fprintf(stderr, "---- dump payload: cb=%Zu ----\n", payload->length);
		fprintf(stderr, "%.*s\n", (int)payload->length, payload->data);
	}
	return;
}
