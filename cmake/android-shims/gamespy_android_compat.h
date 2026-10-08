/*
 * ZH Mobile: force-included into GameSpy's C sources on Android. bionic has no
 * pthread_cancel; GameSpy only uses it to stop its own helper threads, which is
 * irrelevant on Android (the GameSpy servers are gone anyway).
 */
#pragma once
#include <pthread.h>
static inline int pthread_cancel(pthread_t thread) { (void)thread; return 0; }
