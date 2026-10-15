/*
 * Copyright 2026, Galleria authors.
 * Distributed under the terms of the MIT License.
 */


#include "WorkerThread.h"

#include <new>


static const size_t kWorkerStackSize = 8 * 1024 * 1024;


static void*
worker_entry(void* data)
{
	std::function<void()>* function = (std::function<void()>*)data;
	(*function)();
	delete function;
	return NULL;
}


status_t
start_worker_thread(std::function<void()> function, pthread_t* _thread)
{
	std::function<void()>* copy
		= new(std::nothrow) std::function<void()>(std::move(function));
	if (copy == NULL)
		return B_NO_MEMORY;

	pthread_attr_t attributes;
	pthread_attr_init(&attributes);
	pthread_attr_setstacksize(&attributes, kWorkerStackSize);
	if (_thread == NULL)
		pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);

	pthread_t thread;
	int error = pthread_create(&thread, &attributes, &worker_entry, copy);
	pthread_attr_destroy(&attributes);
	if (error != 0) {
		delete copy;
		return error;
	}

	if (_thread != NULL)
		*_thread = thread;
	return B_OK;
}
