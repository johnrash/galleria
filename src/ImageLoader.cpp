/*
 * Copyright 2026, Galleria authors.
 * Distributed under the terms of the MIT License.
 */


#include "ImageLoader.h"

#include <algorithm>

#include <Bitmap.h>
#include <Message.h>
#include <OS.h>

#include "Galleria.h"
#include "ImageFile.h"
#include "WorkerThread.h"


static const int32 kSmallestLevel = 128;


LoadedImage::LoadedImage()
	:
	status(B_NO_INIT),
	width(0),
	height(0)
{
}


LoadedImage::~LoadedImage()
{
	for (size_t i = 0; i < levels.size(); i++)
		delete levels[i];
}


// Returns the smallest level that is still at least as large as the image
// drawn at the given scale; *levelScale is the scale to draw it with.
BBitmap*
LoadedImage::LevelFor(float scale, float* levelScale) const
{
	if (levels.empty())
		return NULL;

	size_t level = 0;
	float factor = 1.0f;
	while (level + 1 < levels.size() && scale <= factor / 2) {
		level++;
		factor /= 2;
	}
	*levelScale = scale / factor;
	return levels[level];
}


size_t
LoadedImage::Bytes() const
{
	size_t bytes = 0;
	for (size_t i = 0; i < levels.size(); i++)
		bytes += levels[i]->BitsLength();
	return bytes;
}


static LoadedImage*
load_image(const entry_ref& ref)
{
	LoadedImage* image = new LoadedImage;
	image->ref = ref;

	BBitmap* bitmap = load_bitmap(ref, &image->status);
	if (bitmap == NULL)
		return image;

	BRect bounds = bitmap->Bounds();
	image->width = bounds.IntegerWidth() + 1;
	image->height = bounds.IntegerHeight() + 1;
	image->levels.push_back(bitmap);

	int32 width = image->width;
	int32 height = image->height;
	while (std::max(width, height) / 2 >= kSmallestLevel) {
		width = (width + 1) / 2;
		height = (height + 1) / 2;
		BBitmap* half = scale_bitmap(image->levels.back(), width, height);
		if (half == NULL)
			break;
		image->levels.push_back(half);
	}
	return image;
}


// #pragma mark -


ImageLoader::ImageLoader(const BMessenger& target)
	:
	fTarget(target),
	fHasRequest(false),
	fHasPrefetch(false),
	fHasInProgress(false),
	fQuit(false)
{
	fHasThread = start_worker_thread([this]() { _Run(); }, &fThread)
		== B_OK;
}


ImageLoader::~ImageLoader()
{
	{
		std::lock_guard<std::mutex> lock(fLock);
		fQuit = true;
	}
	fCondition.notify_all();
	if (fHasThread)
		pthread_join(fThread, NULL);
}


void
ImageLoader::Load(const entry_ref& ref)
{
	{
		std::lock_guard<std::mutex> lock(fLock);
		if (fHasPrefetch && fPrefetch == ref)
			fHasPrefetch = false;
		if (fHasInProgress && fInProgress == ref) {
			// already being decoded; the result is on its way
			fHasRequest = false;
			return;
		}
		fRequest = ref;
		fHasRequest = true;
	}
	fCondition.notify_all();
}


void
ImageLoader::Prefetch(const entry_ref& ref)
{
	{
		std::lock_guard<std::mutex> lock(fLock);
		if (fHasInProgress && fInProgress == ref)
			return;
		fPrefetch = ref;
		fHasPrefetch = true;
	}
	fCondition.notify_all();
}


void
ImageLoader::_Run()
{
	rename_thread(find_thread(NULL), "image loader");

	while (true) {
		entry_ref ref;
		bool prefetch = false;
		{
			std::unique_lock<std::mutex> lock(fLock);
			fCondition.wait(lock, [this] {
				return fQuit || fHasRequest || fHasPrefetch;
			});
			if (fQuit)
				return;
			if (fHasRequest) {
				ref = fRequest;
				fHasRequest = false;
			} else {
				ref = fPrefetch;
				fHasPrefetch = false;
				prefetch = true;
			}
			fInProgress = ref;
			fHasInProgress = true;
		}

		LoadedImage* image = load_image(ref);
		{
			std::lock_guard<std::mutex> lock(fLock);
			fHasInProgress = false;
		}

		BMessage message(kMsgImageLoaded);
		message.AddPointer("image", image);
		message.AddBool("prefetch", prefetch);

		status_t status;
		do {
			status = fTarget.SendMessage(&message, (BHandler*)NULL, 100000);
		} while (status == B_TIMED_OUT && !fQuit);

		if (status != B_OK)
			delete image;
	}
}
