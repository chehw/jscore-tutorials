#ifndef _SKEY_VALUE_PAIR_H_
#define _SKEY_VALUE_PAIR_H_

#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct skey_value_pair
{
	char * key;
	ssize_t cb_value;
	char * value;
}skey_value_pair_t;
skey_value_pair_t * skey_value_pair_new(const char * key, const char * value, ssize_t cb_value);
void skey_value_pair_free(skey_value_pair_t * kvp);

int skey_value_pair_replace_value(skey_value_pair_t * kvp, char * value, ssize_t cb_value);

#ifdef __cplusplus
}
#endif
#endif
