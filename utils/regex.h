#ifndef CHLIB_REGEX_H_
#define CHLIB_REGEX_H_

#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif	

typedef struct regex_context
{
	void * user_data;
	void * priv;
	
	int (* set_pattern)(struct regex_context * regex, const char * pattern);
	ssize_t (* match)(struct regex_context * regex, const char * text, ssize_t cb_text);
}regex_context_t;

regex_context_t * regex_context_init(regex_context_t * regex, void * user_data);
void regex_context_cleanup(regex_context_t * regex);

#ifdef __cplusplus
}
#endif
#endif
