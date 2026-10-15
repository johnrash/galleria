/*
 * Copyright 2026, Galleria authors.
 * Distributed under the terms of the MIT License.
 */
#ifndef IMAGE_LOADER_H
#define IMAGE_LOADER_H


#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <vector>

#include <pthread.h>

#include <Entry.h>
#include <Messenger.h>


class BBitmap;


// A decoded image with precomputed half size versions, so that strongly
// reduced views are drawn from a properly filtered bitmap instead of
// sampling the full size image.
struct LoadedImage {
	entry_ref				ref;
	status_t				status;
	int32					width;
	int32					height;
	std::vector<BBitmap*>	levels;		// [0] is the full size image

							LoadedImage();
							~LoadedImage();

			BBitmap*		Bitmap() const
								{ return levels.empty() ? NULL : levels[0]; }
			BBitmap*		LevelFor(float scale, float* levelScale) const;
			size_t			Bytes() const;
};

typedef std::shared_ptr<LoadedImage> LoadedImagePtr;


// Decodes images on one background thread. A new Load() replaces a pending
// one (only the image the user wants now matters); Prefetch() is served when
// there is nothing else to do. Results arrive as kMsgImageLoaded with
// "image" (pointer to a LoadedImage the receiver owns) and "prefetch".
class ImageLoader {
public:
								ImageLoader(const BMessenger& target);
								~ImageLoader();

			void				Load(const entry_ref& ref);
			void				Prefetch(const entry_ref& ref);

private:
			void				_Run();

private:
			BMessenger			fTarget;
			std::mutex			fLock;
			std::condition_variable fCondition;
			entry_ref			fRequest;
			bool				fHasRequest;
			entry_ref			fPrefetch;
			bool				fHasPrefetch;
			entry_ref			fInProgress;
			bool				fHasInProgress;
			std::atomic<bool>	fQuit;
			pthread_t			fThread;
			bool				fHasThread;
};


#endif	// IMAGE_LOADER_H
