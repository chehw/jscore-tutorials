/*
 * utils.c
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
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <errno.h>
#include <dirent.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>

#include "utils.h"
#include "clib-stack.h"


ssize_t unix_time_to_string(
	const time_t tv_sec, 
	int use_gmtime, 
	const char * time_fmt, 
	char * sz_time, size_t max_size
)
{
	if(NULL == time_fmt) time_fmt = "%Y-%m-%d %H:%M:%S %Z";
	struct tm tm_buf[1], *t = NULL;
	memset(tm_buf, 0, sizeof(tm_buf));
	t = use_gmtime?gmtime_r(&tv_sec, tm_buf):localtime_r(&tv_sec, tm_buf);
	if(NULL == t) {
		perror("unix_time_to_string::gmtime_r/localtime_r");
		return -1;
	}
	
	ssize_t cb = strftime(sz_time, max_size, time_fmt, t);
	return cb;
}

ssize_t utils_load_file(const char *path, const char * filename, unsigned char **p_data, struct stat * st)
{
	debug_printf("== (%s, %s, )", path, filename);
	static const ssize_t MAX_BUFFER_SIZE = PATH_MAX + PATH_MAX;
	
	assert(filename);
	int rc;
	ssize_t cb = -1;
	struct stat _st[1];
	if(NULL == st) st = _st;
	memset(st, 0, sizeof(struct stat));
	
	
	size_t cb_path = 0;
	size_t cb_filename = 0;
	char * path_name = NULL;
	
	cb_filename	= strlen(filename);
	if(path) cb_path = strlen(path);
	
	if((cb_path + cb_filename) > MAX_BUFFER_SIZE) {
		errno = ENOBUFS;
		return -1;
	}
	
	if(cb_path == 0 || filename[0] == '/') {	// no parent dir or use absolute path
		path_name = (char *)filename;
	}else {
		path_name = calloc(cb_path + 1 + cb_filename  + 1, 1);
		assert(path_name);
		
		memcpy(path_name, path, cb_path);
		if(path_name[cb_path - 1] != '/') path_name[cb_path++] = '/';	// add  '/' seperator
		strcpy(path_name + cb_path, filename);	
	}
	
	debug_printf("path_name: %s", path_name);
	
	rc = stat(path_name, st);
	if(rc || (st->st_mode & S_IFMT) != S_IFREG )  {
		fprintf(stderr, "not regular file: '%s'\n", path_name);
		goto label_final;
	}
	
	ssize_t length = st->st_size;
	if(NULL == p_data || length <= 0) { // (null == p_data) can be used to retrieve file stat;
		cb = length;
		goto label_final;
	}
	
	unsigned char *data = *p_data;
	if(NULL == data) {
		data = calloc(length + 1, 1);
		assert(data);
		*p_data = data;
	}
	
	FILE * fp = fopen(path_name, "rb");
	if(NULL == fp) {
		perror(path_name);
		goto label_final;
	}
	cb = fread(data, 1, length, fp);
	fclose(fp);
	if(cb >= 0) data[cb] = '\0';
	
label_final:
	if(path_name && path_name != filename) free(path_name);
	return cb;
}

ssize_t utils_list_folder(const char * path, int recursive, char *** p_path_names) // skip symbolic links to avoid loops
{
	assert(path && path[0]);
	int rc = 0;
	clib_queue_t queue[1];
	memset(queue, 0, sizeof(queue));
	clib_queue_init(queue);
	
	char old_path[PATH_MAX] = "";
	char * p_saved_path = getcwd(old_path, sizeof(old_path));
	if(NULL == p_saved_path) {
		perror("utils_list_folder()::getcwd()");
		return -1;
	}
	
	DIR * dir = opendir(path);
	if(NULL == dir) {
		perror("utils_list_folder()::opendir()");
		return -1;
	}
	
	rc = chdir(path);
	if(rc) {
		perror("utils_list_folder()::chdir()");
		closedir(dir);
		return -1;
	}
	
	ssize_t allocated_size = 4096;
	ssize_t length = 0;
	char ** path_names = calloc(allocated_size, sizeof(*path_names));
	assert(path_names);
	
	#define add_filename(name) ({ int ret = 0; \
			if(length == allocated_size) { 	\
				path_names = realloc(path_names, sizeof(*path_names) * allocated_size * 2); \
				assert(path_names); \
				memset(path_names + allocated_size, 0, sizeof(*path_names) * allocated_size); \
				allocated_size *= 2; \
			} \
			path_names[length++] = strdup(name); \
			ret; })
	
	struct dirent * entry = NULL;
	while((entry = readdir(dir))) {
		if(entry->d_type != DT_REG && entry->d_type != DT_DIR) continue;
		if(entry->d_name[0] == '.') continue;	// skip '.' '..' and hidden files
		if(entry->d_type == DT_REG) {
			add_filename(entry->d_name);
			continue;
		}
		if(!recursive) continue;
		queue->push(queue, strdup(entry->d_name));
	}
	closedir(dir);
	
	char * subfolder = NULL;
	
	while((subfolder = queue->pop(queue))) {
		dir = opendir(subfolder);
		printf("opendir %s = %p\n", subfolder, dir);
		if(NULL == dir) {
			perror("opendir");
			free(subfolder);
			continue;
		}
		char full_name[PATH_MAX] = "";
		while((entry = readdir(dir))) {
			if(entry->d_type != DT_REG && entry->d_type != DT_DIR) continue;
			if(entry->d_name[0] == '.') continue;	// skip '.' '..' and hidden files
			
			snprintf(full_name, sizeof(full_name), "%s/%s", subfolder, entry->d_name);
			if(entry->d_type == DT_REG) {
				add_filename(full_name);
				continue;
			}
			queue->push(queue, strdup(full_name));
		}
		free(subfolder);
		closedir(dir);
	}
	#undef add_filename
	
	clib_queue_cleanup(queue);
	rc = chdir(p_saved_path);
	assert(0 == rc);
	
	if(0 == length) {
		free(path_names);
		path_names = NULL;
	}
	if(path_names) *p_path_names = realloc(path_names, sizeof(*path_names) * length);
	
	return length;
}

/******************************************************
 * TEST Module
 *****************************************************/
#if defined(_TEST_UTILS) && defined(_STAND_ALONE)

int test_list_folder(int argc, char ** argv);
int test_load_file(int argc, char **argv);

int main(int argc, char ** argv)
{
	int rc = 0;
	
	// test unix_time_to_string
	char sz_time[100] = "";
	ssize_t cb = 0;
	struct timespec ts[1] = {{ .tv_sec = 0, }};
	clock_gettime(CLOCK_REALTIME, ts);

	
	cb = unix_time_to_string(ts->tv_sec, 0, NULL, sz_time, sizeof(sz_time));
	assert(cb > 0);
	printf("-- localtime: %s\n", sz_time);
	
	unix_time_to_string(ts->tv_sec, 1, NULL, sz_time, sizeof(sz_time));
	assert(cb > 0);
	printf("-- gmtime   : %s\n", sz_time);
	
	// test_list_folder
	rc = test_list_folder(argc, argv);
	assert(0 == rc);
	
	// test_load_file
	rc = test_load_file(argc, argv);
	assert(0 == rc);
	
	
	// test utils::list_folder
	
	
	return 0;
}

int test_list_folder(int argc, char ** argv)
{
	const char * document_root = ".";
	int recursive = 1;
	
	if(argc > 1) document_root = argv[1];
	if(argc > 2) recursive = atoi(argv[2]);
	printf("\n==== TEST %s(root_path=%s, recursive=%d) ====\n", __FUNCTION__, document_root, recursive);
	
	// test utils::list_folder
	char ** filelist = NULL;
	ssize_t count = utils_list_folder(document_root, recursive, &filelist);
	printf("count = %d\n", (int)count);
	assert(filelist);
	for(int i = 0; i < count; ++i) {
		printf("%.3d: %s\n", i, filelist[i]);
		free(filelist[i]);
	}
	free(filelist);
	return 0;
}

int test_load_file(int argc, char **argv)
{
	const char * document_root = ".";
	const int recursive = 0;
	
	if(argc > 1) document_root = argv[1];
	printf("\n==== TEST %s(root_path=%s, recursive=%d) ====\n", __FUNCTION__, document_root, recursive);
	
	char ** filelist = NULL;
	ssize_t count = utils_list_folder(document_root, recursive, &filelist);
	printf("count = %d\n", (int)count);
	assert(filelist);
	for(int i = 0; i < count; ++i) {
		printf("%.3d: %s\n", i, filelist[i]);
		
		unsigned char * data = NULL;
		struct stat st[1];
		memset(st, 0, sizeof(st));
		ssize_t cb_data = utils_load_file(document_root, filelist[i], &data, st);
		printf("data=%p, cb_data:%Zd, filename: '%s'\n"
			"\t file_size: %Zu, LastMod: %ld.%.9ld\n",
			data, cb_data, filelist[i],
			(size_t)st->st_size, (long)st->st_mtim.tv_sec, (long)st->st_mtim.tv_nsec);
		if(data) free(data);
		free(filelist[i]);
	}
	free(filelist);
	return 0;
}
#endif
