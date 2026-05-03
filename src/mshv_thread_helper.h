/* MSHV pthread helper — bump stack size on macOS.
 *
 * macOS pthreads default to ~512 KB of stack; the WSJT-X-derived FT8/MSK
 * decoders allocate large arrays on the stack and overflow. Match the Linux
 * 8 MB default so __chkstk_darwin doesn't trip.
 *
 * Use mshv_pthread_create() in place of pthread_create(thr, NULL, fn, arg)
 * for any thread that runs MSHV decoder/generator/display work.
 */
#ifndef MSHV_THREAD_HELPER_H
#define MSHV_THREAD_HELPER_H

#include <pthread.h>

static inline int mshv_pthread_create(pthread_t *thr, void *(*fn)(void *), void *arg)
{
#if defined _MACOS_
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 8 * 1024 * 1024);
    int r = pthread_create(thr, &attr, fn, arg);
    pthread_attr_destroy(&attr);
    return r;
#else
    return pthread_create(thr, NULL, fn, arg);
#endif
}

#endif
