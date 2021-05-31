#ifndef CHLIB_UTILS_H_
#define CHLIB_UTILS_H_

#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif

#include <sys/stat.h>
#include <stdarg.h>
#include <time.h>

#define TERM_COLOR_DEBUG 	"\e[33m"
#define TERM_COLOR_DEFAULT 	"\e[39m"
#ifdef _DEBUG
#define debug_printf(fmt, ...) \
	fprintf(stderr, TERM_COLOR_DEBUG "[DEBUG]: %s(%d)::%s(): " fmt TERM_COLOR_DEFAULT "\n", \
		__FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__)
#else
#define debug_printf(fmt, ...) do { } while(0)
#endif

ssize_t unix_time_to_string(
	const time_t tv_sec, 
	int use_gmtime, 
	const char * time_fmt, // nullable, default: "%Y-%m-%d %H:%M:%S %Z"
	char * sz_time, size_t max_size
);
ssize_t utils_load_file(const char *path, const char *filename, unsigned char **p_data, struct stat * st);
ssize_t utils_list_folder(const char * path, int recursive, char *** p_path_names);

#include <json-c/json.h>
typedef char * string;
#define json_get_value_default(jobj, type, key, defval) ({	\
		type value = (type)defval;	\
		json_object * jvalue = NULL;	\
		json_bool ok = json_object_object_get_ex(jobj, #key, &jvalue);	\
		if(ok && jvalue) value = (type)json_object_get_##type(jvalue);	\
		value;	\
	})
#define json_get_value(jobj, type, key) json_get_value_default(jobj, type, key, 0)
#define json_set_value(jobj, type, key, value) json_object_object_add(jobj, #key, json_object_new_##type(value))

#define is_white_char(c) ( (c) == ' ' || (c) == '\t' || (c) == '\r' || (c) == '\n' ) 
#define trim_left(p) ({ \
		char c;	\
		while((c = *(p)) && is_white_char(c)) ++p; \
		p; \
	})
#define trim_right(p, p_end) ({ \
		while(p_end > p && is_white_char(p_end[-1])) --p_end; \
		p; \
	}) 

#ifdef __cplusplus
}
#endif
#endif

