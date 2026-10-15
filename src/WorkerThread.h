/*
 * Copyright 2026, Galleria authors.
 * Distributed under the terms of the MIT License.
 */
#ifndef WORKER_THREAD_H
#define WORKER_THREAD_H


#include <functional>

#include <pthread.h>

#include <SupportDefs.h>


// Starts a thread with a large stack. Threads that run translators need it:
// the JPEG 2000 translator alone puts a 256 KiB buffer on the stack, which is
// the whole default thread stack on Haiku. The stack is committed lazily, so
// the size costs only address space.
// With _thread == NULL the thread is detached.
status_t	start_worker_thread(std::function<void()> function,
				pthread_t* _thread = NULL);


#endif	// WORKER_THREAD_H
