#ifndef EMAIL_SENDER_H_
#define EMAIL_SENDER_H_

#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif
#include <limits.h>
#include <time.h>

#include "avl_tree.h"
#include "skey_value_pair.h"
#include "auto_buffer.h"

/*
 * rfc4616 The PLAIN SASL Mechanism
 * https://www.ietf.org/rfc/rfc4616.txt
*/
#define SASL_PLAIN_AUTH_NAME_LENGTH (256)
#define EMAIL_ADDRESS_MAX_LENGTH (256)

/**
 * @defgroup email_sender
 * @{
 * @}
**/

/**
 * @ingroup email_sender
 * @{
**/
enum smtp_security_mode
{
	smtp_security_mode_default = 0,		// smtp_security_mode_force_tls
	smtp_security_mode_try_tls = 1,
	smtp_security_mode_ssl = 2, 		// default port 465, use legacy smtps protocol
	smtp_security_mode_force_tls = 3,	// default port 587
};

enum email_sender_user_agent
{
	email_sender_user_agent_default, 
	email_sender_user_agent_libcurl = 1,
	email_sender_interactive, // tcp with io-redirect
};
/**
 * @}
**/



/**
 * @ingroup email_sender
 * @defgroup recipents
 * @{
**/
enum email_address_type
{
	email_address_type_mail_from,
	email_address_type_to,
	email_address_type_cc,
	email_address_type_bcc,
};
const char * email_address_type_to_string(enum email_address_type type);

struct email_address_data
{
	char addr[EMAIL_ADDRESS_MAX_LENGTH];	// format:  display_name (comment) <email_addr>
	int cb_addr;
	enum email_address_type type;
	
	///< email_addr: The part enclosed by "<" and ">". If there is no'<' character in addr, then the entire addr would be regarded as email_addr
	char email_addr[EMAIL_ADDRESS_MAX_LENGTH]; 	
	
	char display_name[EMAIL_ADDRESS_MAX_LENGTH]; 	// nullable
	char comment[EMAIL_ADDRESS_MAX_LENGTH];			// The part enclosed by "(" and ")", nullable
};
struct email_address_data * email_address_data_set(
	struct email_address_data * email_addr, 
	enum email_address_type type, 
	const char * addr, int cb_addr);
void email_address_data_free(struct email_address_data * addr);
/**
 * @}
**/


enum email_address_duplicates_policy // if duplicate addresses found in ( TO, CC, BCC )
{
	email_address_duplicates_discard,
	email_address_duplicates_replace_with_latest
};

/**
 * @ingroup email_sender
 * @defgroup address_list
 * @{
**/
struct email_address_list
{
	void * user_data;
	struct email_address_data mail_from_addr[1];
	
	enum email_address_duplicates_policy dup_policy;
	size_t max_size;
	size_t num_recipients;
	struct email_address_data ** recipients_addrs; // malloc and store all types of recipients_addrs ( TO, CC, BCC )
	void * addrs_root;	// tsearch-root or avl_tree
	
	// public methods
	int (* add)(struct email_address_list * list, enum email_address_type type, const char * addr); ///< @return 0 on success, -1 on error, 1 on duplicated
	int (* remove)(struct email_address_list * list, const char * addr); ///<@return 0 on success, -1 on error.
	struct email_address_data * (* find)(struct email_address_list * list, const char * addr); ///< @return pointer to a recipents_addr if exists
	void (* clear)(struct email_address_list * list); ///< @brief remove all recipients
};
struct email_address_list * email_address_list_init(struct email_address_list * list, void * user_data);
void email_address_list_cleanup(struct email_address_list * list);
/**
 * @}
**/


/**
 * @ingroup email_sender
 * @defgroup email_header
 * @{
**/
struct email_header
{
	avl_tree_t root[1]; // base object
	
	void * user_data;
	size_t num_items;
	
	int (* add)(struct email_header * hdr, const char * key, const char * value);
	int (* remove)(struct email_header * hdr, const char * key);
	int (* foreach)(struct email_header * hdr, int (* callback)(const char * key, const char * value, void * user_data), void * user_data);
	void (* clear)(struct email_header * hdr);
};
struct email_header * email_header_init(struct email_header * hdr, void * user_data);
void email_header_cleanup(struct email_header * hdr);
/**
 * @}
**/


/**
 * @ingroup email_sender
 * @{
**/
struct email_sender_context
{
	void * priv;
	void * user_data;
	
	int security_mode;
	char url[PATH_MAX];
	char username[SASL_PLAIN_AUTH_NAME_LENGTH];
	char password[SASL_PLAIN_AUTH_NAME_LENGTH];
	struct email_address_list addr_list[1];
	
	struct email_header hdr[1];
	auto_buffer_t body[1];
	
	auto_buffer_t payload[1];
	int prepared;
	
	// public functions for setup
	int (* set_smtp_server)(struct email_sender_context * email, enum smtp_security_mode mode, const char * server_name, unsigned int port);
	int (* set_auth_plain)(struct email_sender_context * email, const char * username, const char * password);
	int (* set_from_addr)(struct email_sender_context * email, const char * from_addr);
	int (* add_recipents)(struct email_sender_context * email, enum email_address_type type, ...) __attribute__((__sentinel__(0)));
	
	// public functions for email headers and body
	int (* add_header)(struct email_sender_context * email, const char * key, const char * value); // add key-value pairs
	int (* add_body)(struct email_sender_context * email, const char * body, size_t cb_body);
	void (* clear)(struct email_sender_context * email);	// remove all lines
	
	// utils
	/**
	 * prepare_payload()
	 * @brief generate SMTP DATA block
	 * According to rfc2821, any "." in the body will be escaped as ".."
	 * 
	 * Many libraries would do this trick underneath,
	 * set the 'escape_dot_char' flag to 1 if need to control manually.
	 */
	int (* prepare_payload)(struct email_sender_context * email, 
		int escape_dot_char,	// 0: do not escape, the backend user-agent would do the trick;   1: need to escape '.' manually
		auto_buffer_t * payload, const struct timespec * timestamp);
	
	// virtual functions, overidable
	int (* send)(struct email_sender_context * email);
	void (* cleanup)(struct email_sender_context * email);
};
/**
 * @}
**/

/**
 * @ingroup email_sender
 * @{
**/
void email_sender_context_cleanup(struct email_sender_context * email);
struct email_sender_context * email_sender_context_init(
	struct email_sender_context * email, 
	enum email_sender_user_agent agent, 
	void * user_data);
/**
 * @}
**/


/**
 * misc utils
**/
/* 
 * generate rfc2822-compliant date string 
*/
ssize_t email_utils_generate_date(char sz_date[static 1], size_t size, const struct timespec * timestamp);
void email_sender_context_dump(const struct email_sender_context * email);

#ifdef __cplusplus
}
#endif
#endif
