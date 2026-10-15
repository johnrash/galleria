/*
 * Copyright 2026, Galleria authors.
 * Distributed under the terms of the MIT License.
 *
 * Generates thumbnails on worker threads and keeps them in a disk cache.
 *
 * The whole cache lives in one directory, ~/config/cache/Galleria, and can be
 * deleted at any time (File > Clear thumbnail cache, or rm -r). Thumbnails are
 * PNG files in a subdirectory per size class (128, 256, 512), named after a
 * hash of the image path; attributes on each PNG record the source's path,
 * size and modification time, so a changed image gets a fresh thumbnail.
 */
#ifndef THUMBNAIL_LOADER_H
#define THUMBNAIL_LOADER_H


#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <set>
#include <vector>

#include <pthread.h>

#include <Entry.h>
#include <Messenger.h>
#include <Path.h>
#include <String.h>


class BBitmap;


struct ThumbnailRequest {
	int32		id;
	entry_ref	ref;
	int32		bucket;			// 128, 256 or 512
	off_t		size;
	int64		modified;
};


// Results are sent to the target as kMsgThumbnailReady with "id" (int32),
// "bucket" (int32), "bitmap" (pointer to a BBitmap the receiver owns, or
// NULL if the image could not be read), "width" and "height" (int32, the
// image's dimensions).
class ThumbnailLoader {
public:
								ThumbnailLoader(const BMessenger& target);
								~ThumbnailLoader();

			// Replaces all pending requests (the first is served first).
			void				SetRequests(
									std::vector<ThumbnailRequest>& requests);
			int32				PendingCount();

	static	int32				BucketFor(int32 thumbnailSize);

	static	BPath				CacheDirectory();
	static	status_t			ClearCache();
	static	void				GetCacheUsage(int32* files, off_t* bytes);

private:
			void				_Worker();
			BBitmap*			_Generate(const ThumbnailRequest& request,
									int32* width, int32* height,
									status_t* status);
			BBitmap*			_ReadCached(const char* cachePath,
									const char* sourcePath,
									const ThumbnailRequest& request,
									int32* width, int32* height);
			void				_WriteCached(const char* cachePath,
									const char* sourcePath,
									const ThumbnailRequest& request,
									BBitmap* thumbnail, int32 width,
									int32 height);

private:
			BMessenger			fTarget;
			std::mutex			fLock;
			std::condition_variable fCondition;
			std::deque<ThumbnailRequest> fQueue;
			std::multiset<int32> fInProgress;
			std::atomic<bool>	fQuit;
			std::vector<pthread_t> fThreads;
			BString				fCacheRoot;
};


#endif	// THUMBNAIL_LOADER_H
