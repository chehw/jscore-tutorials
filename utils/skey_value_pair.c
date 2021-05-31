/*
 * skey_value_pair.c
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

#include "skey_value_pair.h"

skey_value_pair_t * skey_value_pair_new(const char * key, const char * value, ssize_t cb_value)
{
	assert(key);
	skey_value_pair_t * kvp = calloc(1, sizeof(*kvp));
	assert(kvp);
	
	kvp->key = strdup(key);
	if(value) {
		if(cb_value == -1) cb_value = strlen(value);
		assert(cb_value >= 0);
		
		kvp->value = calloc(cb_value + 1, 1);
		memcpy(kvp->value, value, cb_value);
		kvp->cb_value = cb_value;
	}
	return kvp;
}
void skey_value_pair_free(skey_value_pair_t * kvp)
{
	if(NULL == kvp) return;
	if(kvp->key) free(kvp->key);
	if(kvp->value) free(kvp->value);
	free(kvp);
}

int skey_value_pair_replace_value(skey_value_pair_t * kvp, char * value, ssize_t cb_value)
{
	if(NULL == kvp) return -1;
	if(kvp->value) { free(kvp->value); kvp->value = NULL; kvp->cb_value = 0; }
	
	kvp->value = value;
	kvp->cb_value = cb_value;
	return 0;
}

#if defined(_TEST_SKEY_VALUE_PAIR) && defined(_STAND_ALONE)
int main(int argc, char **argv)
{
	
	return 0;
}
#endif
