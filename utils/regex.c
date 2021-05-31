/*
 * regex.c
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


#include <pcre.h>
#include "regex.h"

#define NUM_PCRE_SUBSTR_VEC (256)
typedef struct regex_private
{
	regex_context_t * regex;
	pcre * re;
	pcre_extra * re_extra;
	
	int num_matched;
	int substr_index_vec[NUM_PCRE_SUBSTR_VEC];
	
	int err_code;
	int err_offset;
	const char * err_msg;
}regex_private_t;
static regex_private_t * regex_private_new(regex_context_t * regex) 
{
	assert(regex);
	regex_private_t * priv = calloc(1, sizeof(*priv));
	assert(priv);
	priv->regex = regex;
	regex->priv = priv;
	
	return priv;
}
static void regex_private_reset(regex_private_t * priv) 
{
	if(NULL == priv) return;
	if(priv->re) {
		pcre_free(priv->re);
		priv->re = NULL;
	}
	
	if(priv->re_extra) {
	#ifdef PCRE_CONFIG_JIT
		pcre_free_study(priv->re_extra);
	#else
		pcre_free(priv->re_extra);
	#endif
		priv->re_extra = NULL;
	}
	
	memset(priv->substr_index_vec, 0, sizeof(priv->substr_index_vec));
	priv->err_code = 0;
	priv->err_offset = -1;
	priv->err_msg = NULL;
	return;
}

static void regex_private_free(regex_private_t * priv) 
{
	if(NULL == priv) return;
	regex_private_reset(priv);
	free(priv);
	return;
}



static int regex_set_pattern(regex_context_t * regex, const char * pattern)
{
	assert(regex && regex->priv);
	regex_private_t * priv = regex->priv;
	
	regex_private_reset(priv);
	
	pcre * re = pcre_compile(pattern, 0, &priv->err_msg, &priv->err_offset, NULL);
	if(NULL == re) {
		fprintf(stderr, "[ERROR]: Could not parse pattern '%s': err_offset=%d, err_msg=%s\n",
			pattern, 
			priv->err_offset,
			priv->err_msg);
		return -1;
	}
	priv->err_msg = NULL;
	pcre_extra * re_extra = pcre_study(re, 0, &priv->err_msg);
	if(priv->err_msg) {
		fprintf(stderr, "[ERROR]: Could not optimize pattern '%s': \err_msg=%s\n",
			pattern,
			priv->err_msg);
		pcre_free(re);
		if(re_extra) {
			pcre_free_study(re_extra);
		}
		return -1;
	}
	priv->re = re;
	priv->re_extra = re_extra;
	
	return 0;
}

static ssize_t regex_match(regex_context_t *regex, const char * text, ssize_t cb_text)
{
	assert(regex && regex->priv);
	assert(text);
	regex_private_t * priv = regex->priv;
	
	if(NULL == priv->re) {
		fprintf(stderr, "[ERROR]: pattern not set.\n");
		return -1;
	}
	if(cb_text == -1) cb_text = strlen(text);
	if(cb_text <= 0) return 0;

	pcre * re = priv->re;
	pcre_extra * re_extra = priv->re_extra;
	memset(priv->substr_index_vec, 0, sizeof(priv->substr_index_vec));
	priv->err_code = 0;
	priv->err_offset = -1;
	priv->err_msg = NULL;
	priv->num_matched = 0;
	
	int ret = pcre_exec(re, re_extra, text, cb_text, 0, 0, 
		priv->substr_index_vec, 
		(int)(sizeof(priv->substr_index_vec) / sizeof(priv->substr_index_vec[0]))
	);
	
	if(ret < 0) {
		priv->err_code = ret;
		switch(ret) {
			case PCRE_ERROR_NOMATCH      : priv->err_msg = "String did not match the pattern";        break;
			case PCRE_ERROR_NULL         : priv->err_msg = "Something was null";                      break;
			case PCRE_ERROR_BADOPTION    : priv->err_msg = "A bad option was passed";                 break;
			case PCRE_ERROR_BADMAGIC     : priv->err_msg = "Magic number bad (compiled re corrupt?)"; break;
			case PCRE_ERROR_UNKNOWN_NODE : priv->err_msg = "Something kooky in the compiled re";      break;
			case PCRE_ERROR_NOMEMORY     : priv->err_msg = "Ran out of memory";                       break;
			default                      : priv->err_msg = "Unknown error";                           break;
		}	
	}else {
		priv->num_matched = ret;
	}
	return priv->num_matched;
}

regex_context_t * regex_context_init(regex_context_t * regex, void * user_data)
{
	if(NULL == regex) regex = calloc(1, sizeof(*regex));
	assert(regex);
	
	regex->set_pattern = regex_set_pattern;
	regex->match = regex_match;
	
	regex_private_t * priv = regex_private_new(regex);
	assert(priv && regex->priv == priv);
	return regex;
}

void regex_context_cleanup(regex_context_t * regex)
{
	if(NULL == regex) return;
	regex_private_free(regex->priv);
	regex->priv = NULL;
	return;
}


#if defined(_TEST_REGEX) && defined(_STAND_ALONE)
// https://murashun.jp/article/programming/regular-expression.html
/* 使用頻度の高い正規表現式
 - Email アドレス (RFC準拠ではない)
	^\w+([-+.]\w+)*@\w+([-.]\w+)*\.\w+([-.]\w+)*$
 - URL
	^https?://([\w-]+\.)+[\w-]+(/[\w-./?%&=]*)?$
 - ドメイン名
	^[a-zA-Z0-9][a-zA-Z0-9-]{1,61}[a-zA-Z0-9]\.[a-zA-Z-]{2,}$
 - 固定電話番号
	^0\d(-\d{4}|\d-\d{3}|\d\d-\d\d|\d{3}-\d)-\d{4}$
 - 携帯電話番号
	^0[789]0-\d{4}-\d{4}$
 - IP 電話番号
	^050-\d{4}-\d{4}$
 - フリーダイヤル
	^(0120|0800)-\d{3}-\d{3}$
 - 日付 (YYYY-MM-DD形式)
	^\d{4}-\d\d-\d\d$
 - 郵便番号
	^\d{3}-\d{4}$
*/

static const char email_pattern[] = "^\\w+([-+.]\\w+)*@\\w+([-.]\\w+)*\\.\\w+([-.]\\w+)*$";	// charater '\' should be escaped as '\\' 

int pcre_example(int argc, char ** argv);
int main(int argc, char **argv)
{
//	pcre_example(argc, argv);
	regex_context_t * regex = regex_context_init(NULL, NULL);
	assert(regex);

	int rc = regex->set_pattern(regex, email_pattern);
	assert(0 == rc);
	
	for(int i = 1; i < argc; ++i) {
		const char * text = argv[i];
		int num_matched = regex->match(regex, text, strlen(text));
		if(num_matched > 0) {
			printf("\e[32m[Matched OK]\e[39m  <== '%s'\n", text);
		}else {
			printf("\e[31m[Matched NG]\e[39m  <== '%s'\n", text);
		}
	}
	
	regex_context_cleanup(regex);
	free(regex);
	return 0;
}

/* 
 * Example: 
 *  origin: https://www.mitchr.me/SS/exampleCode/AUPG/pcre_example.c.html
 *  modified by: hongwei.che@gmail.com @2021/04/21 01:19:01 JST
 */
int pcre_example(int argc, char ** argv)
{
	pcre * re = NULL;
	pcre_extra *re_extra = NULL;
	
	
	const char * err_msg = NULL;
	int err_offset = -1;
	re = pcre_compile(email_pattern, 0, &err_msg, &err_offset, NULL);
	if(NULL == re) {
		fprintf(stderr, "ERROR: compile failed @%d, err_msg=%s\n", err_offset, err_msg);
		exit(1);
	}
	
	err_msg = NULL;
	re_extra = pcre_study(re, 0, &err_msg);
	if(err_msg) {
		fprintf(stderr, "ERROR: pcre_study failed, err_msg=%s\n", err_msg);
		exit(1);
	}
	
	
	char * test_string = "chehw.1@nour.global";
	if(argc > 1) test_string = argv[1];
	
	char * line = test_string;
	int sub_strs[30];
	int ret = pcre_exec(re, re_extra, line, strlen(line), 0, 0, sub_strs, 30);
	if(ret < 0) { // Something bad happened..
		switch(ret) {
		case PCRE_ERROR_NOMATCH      : printf("String did not match the pattern\n");        break;
		case PCRE_ERROR_NULL         : printf("Something was null\n");                      break;
		case PCRE_ERROR_BADOPTION    : printf("A bad option was passed\n");                 break;
		case PCRE_ERROR_BADMAGIC     : printf("Magic number bad (compiled re corrupt?)\n"); break;
		case PCRE_ERROR_UNKNOWN_NODE : printf("Something kooky in the compiled re\n");      break;
		case PCRE_ERROR_NOMEMORY     : printf("Ran out of memory\n");                       break;
		default                      : printf("Unknown error\n");                           break;
		} /* end switch */
	} else {
		printf("Result: We have a match!\n");
	}
	
	pcre_free(re);
	if(re_extra) pcre_free_study(re_extra);
	return 0;
}
#endif

