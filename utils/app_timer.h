#ifndef CHLIB_APP_TIMER_H_
#define CHLIB_APP_TIMER_H_

#ifdef __cplusplus
extern "C" {
#endif
typedef struct app_timer
{
	double begin;
	double end;
}app_timer_t;
app_timer_t * app_timer_start(app_timer_t * timer);
double app_timer_get_elapsed(app_timer_t * timer);
double app_timer_stop(app_timer_t * timer);

app_timer_t * app_timer_get_default(void);
#ifdef __cplusplus
}
#endif
#endif
