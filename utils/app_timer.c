/*
 * app_timer.c
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

#include <pthread.h>
#include <unistd.h>
#include <time.h>

#include "app_timer.h"

/***************************
 * app timer
**************************/
static pthread_once_t s_once_key = PTHREAD_ONCE_INIT;
static pthread_key_t s_tls_key;	// thread local storage

static void init_app_timer_for_thread(void) {
	int rc = 0;
	rc = pthread_key_create(&s_tls_key, NULL);
	assert(0 == rc);
	
	app_timer_t * timer = calloc(1, sizeof(*timer));	// allocate a default app_timer per thread
	assert(timer);
	rc = pthread_setspecific(s_tls_key, timer);
	assert(0 == rc);
	return;
}
app_timer_t * app_timer_get_default(void) 
{
	pthread_once(&s_once_key, init_app_timer_for_thread);
	return pthread_getspecific(s_tls_key);
}

app_timer_t * app_timer_start(app_timer_t * timer)
{
	if(NULL == timer) timer = app_timer_get_default();
	struct timespec ts= { 0 };
	clock_gettime(CLOCK_MONOTONIC, &ts);
	timer->begin = (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
	return timer;
}
double app_timer_get_elapsed(app_timer_t * timer)
{
	if(NULL == timer) timer = app_timer_get_default();
	struct timespec ts= { 0 };
	clock_gettime(CLOCK_MONOTONIC, &ts);
	timer->end = (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
	return (timer->end - timer->begin);
}
double app_timer_stop(app_timer_t * timer)
{
	if(NULL == timer) timer = app_timer_get_default();
	struct timespec ts= { 0 };
	clock_gettime(CLOCK_MONOTONIC, &ts);
	timer->end = (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
	
	// reset timer
	timer->end -= timer->begin;
	timer->begin = 0;
	return timer->end;
}

