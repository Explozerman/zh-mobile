/*
 * ZH Mobile: Android's libc (bionic) dropped the obsolete <sys/timeb.h>.
 * The engine only includes it for struct timeb / ftime(); provide both.
 */
#pragma once
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

struct timeb {
	time_t time;
	unsigned short millitm;
	short timezone;
	short dstflag;
};

static inline int ftime(struct timeb *tp)
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	tp->time = ts.tv_sec;
	tp->millitm = (unsigned short)(ts.tv_nsec / 1000000);
	tp->timezone = 0;
	tp->dstflag = 0;
	return 0;
}

#ifdef __cplusplus
}
#endif
