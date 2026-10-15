/*
 * Copyright 2026, Galleria authors.
 * Distributed under the terms of the MIT License.
 */


#include "ThumbnailLoader.h"

#include <algorithm>

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <Bitmap.h>
#include <BitmapStream.h>
#include <Directory.h>
#include <File.h>
#include <FindDirectory.h>
#include <Message.h>
#include <NodeInfo.h>
#include <OS.h>
#include <TranslatorFormats.h>
#include <TranslatorRoster.h>

#include "Galleria.h"
#include "ImageFile.h"
#include "WorkerThread.h"


static const char* kAttrPath = "Galleria:path";
static const char* kAttrSize = "Galleria:size";
static const char* kAttrModified = "Galleria:modified";
static const char* kAttrWidth = "Galleria:width";
static const char* kAttrHeight = "Galleria:height";

static const int32 kMaxWorkers = 4;


static uint64
hash_path(const char* path)
{
	// FNV-1a, 64 bit
	uint64 hash = 14695981039346656037ULL;
	for (const uint8* p = (const uint8*)path; *p != '\0'; p++) {
		hash ^= *p;
		hash *= 1099511628211ULL;
	}
	return hash;
}


template<typename T>
static bool
read_attribute(BNode& node, const char* name, type_code type, T& value)
{
	return node.ReadAttr(name, type, 0, &value, sizeof(T)) == sizeof(T);
}


static void
remove_recursively(BEntry& entry)
{
	if (entry.IsDirectory()) {
		// collect first: removing while iterating may skip entries
		BDirectory directory(&entry);
		std::vector<entry_ref> children;
		entry_ref ref;
		while (directory.GetNextRef(&ref) == B_OK)
			children.push_back(ref);
		for (size_t i = 0; i < children.size(); i++) {
			BEntry child(&children[i]);
			remove_recursively(child);
		}
	}
	entry.Remove();
}


static void
add_usage(BEntry& entry, int32* files, off_t* bytes)
{
	if (entry.IsDirectory()) {
		BDirectory directory(&entry);
		BEntry child;
		while (directory.GetNextEntry(&child) == B_OK)
			add_usage(child, files, bytes);
		return;
	}

	off_t size;
	if (entry.GetSize(&size) == B_OK) {
		(*files)++;
		*bytes += size;
	}
}


// #pragma mark -


ThumbnailLoader::ThumbnailLoader(const BMessenger& target)
	:
	fTarget(target),
	fQuit(false)
{
	fCacheRoot = CacheDirectory().Path();

	system_info info;
	int32 count = 1;
	if (get_system_info(&info) == B_OK)
		count = std::max((int32)1, std::min((int32)info.cpu_count, kMaxWorkers));

	for (int32 i = 0; i < count; i++) {
		pthread_t thread;
		if (start_worker_thread([this]() { _Worker(); }, &thread) == B_OK)
			fThreads.push_back(thread);
	}
}


ThumbnailLoader::~ThumbnailLoader()
{
	{
		std::lock_guard<std::mutex> lock(fLock);
		fQuit = true;
		fQueue.clear();
	}
	fCondition.notify_all();

	for (size_t i = 0; i < fThreads.size(); i++)
		pthread_join(fThreads[i], NULL);
}


void
ThumbnailLoader::SetRequests(std::vector<ThumbnailRequest>& requests)
{
	{
		std::lock_guard<std::mutex> lock(fLock);
		fQueue.clear();
		for (size_t i = 0; i < requests.size(); i++) {
			if (fInProgress.find(requests[i].id) == fInProgress.end())
				fQueue.push_back(requests[i]);
		}
	}
	fCondition.notify_all();
}


int32
ThumbnailLoader::PendingCount()
{
	std::lock_guard<std::mutex> lock(fLock);
	return (int32)(fQueue.size() + fInProgress.size());
}


/*static*/ int32
ThumbnailLoader::BucketFor(int32 thumbnailSize)
{
	if (thumbnailSize <= 128)
		return 128;
	if (thumbnailSize <= 256)
		return 256;
	return 512;
}


/*static*/ BPath
ThumbnailLoader::CacheDirectory()
{
	BPath path;
	if (find_directory(B_USER_CACHE_DIRECTORY, &path, true) != B_OK)
		path.SetTo("/boot/home/config/cache");
	path.Append(kApplicationName);
	return path;
}


/*static*/ status_t
ThumbnailLoader::ClearCache()
{
	BEntry entry(CacheDirectory().Path());
	if (!entry.Exists())
		return B_OK;

	remove_recursively(entry);
	return entry.Exists() ? B_ERROR : B_OK;
}


/*static*/ void
ThumbnailLoader::GetCacheUsage(int32* files, off_t* bytes)
{
	*files = 0;
	*bytes = 0;
	BEntry entry(CacheDirectory().Path());
	if (entry.Exists())
		add_usage(entry, files, bytes);
}


void
ThumbnailLoader::_Worker()
{
	// thumbnails must never make the user interface sluggish
	set_thread_priority(find_thread(NULL), B_LOW_PRIORITY);
	rename_thread(find_thread(NULL), "thumbnail worker");

	while (true) {
		ThumbnailRequest request;
		{
			std::unique_lock<std::mutex> lock(fLock);
			fCondition.wait(lock, [this] {
				return fQuit || !fQueue.empty();
			});
			if (fQuit)
				return;
			request = fQueue.front();
			fQueue.pop_front();
			fInProgress.insert(request.id);
		}

		int32 width = 0;
		int32 height = 0;
		status_t status = B_OK;
		BBitmap* bitmap = _Generate(request, &width, &height, &status);

		BMessage message(kMsgThumbnailReady);
		message.AddInt32("id", request.id);
		message.AddInt32("bucket", request.bucket);
		message.AddPointer("bitmap", bitmap);
		message.AddInt32("width", width);
		message.AddInt32("height", height);
		message.AddInt32("status", status);

		{
			std::lock_guard<std::mutex> lock(fLock);
			fInProgress.erase(fInProgress.find(request.id));
		}

		// The window may be busy (or quitting and waiting for us): never
		// block forever on a full port.
		status_t sendStatus;
		do {
			sendStatus = fTarget.SendMessage(&message, (BHandler*)NULL,
				100000);
		} while (sendStatus == B_TIMED_OUT && !fQuit);

		if (sendStatus != B_OK)
			delete bitmap;
	}
}


BBitmap*
ThumbnailLoader::_Generate(const ThumbnailRequest& request, int32* width,
	int32* height, status_t* status)
{
	BPath sourcePath(&request.ref);
	*status = sourcePath.InitCheck();
	if (*status != B_OK)
		return NULL;

	// Never cache thumbnails of the cache itself: browsing the cache folder
	// would otherwise keep adding files to it.
	BString cacheRootPrefix(fCacheRoot);
	cacheRootPrefix << "/";
	bool cacheable = !BString(sourcePath.Path()).StartsWith(cacheRootPrefix);

	BString cachePath;
	cachePath.SetToFormat("%s/%" B_PRId32 "/%016" B_PRIx64 ".png",
		fCacheRoot.String(), request.bucket, hash_path(sourcePath.Path()));

	if (cacheable) {
		BBitmap* cached = _ReadCached(cachePath, sourcePath.Path(), request,
			width, height);
		if (cached != NULL)
			return cached;
	}

	BBitmap* image = load_bitmap(request.ref, status);
	if (image == NULL)
		return NULL;

	BRect bounds = image->Bounds();
	*width = bounds.IntegerWidth() + 1;
	*height = bounds.IntegerHeight() + 1;

	int32 thumbWidth, thumbHeight;
	fit_size(*width, *height, request.bucket, request.bucket, thumbWidth,
		thumbHeight);
	if (thumbWidth == *width && thumbHeight == *height) {
		// small images are their own thumbnail, and need no cache entry
		return image;
	}

	BBitmap* thumbnail = scale_bitmap(image, thumbWidth, thumbHeight);
	delete image;
	if (thumbnail == NULL) {
		*status = B_NO_MEMORY;
		return NULL;
	}

	if (cacheable) {
		_WriteCached(cachePath, sourcePath.Path(), request, thumbnail, *width,
			*height);
	}
	return thumbnail;
}


BBitmap*
ThumbnailLoader::_ReadCached(const char* cachePath, const char* sourcePath,
	const ThumbnailRequest& request, int32* width, int32* height)
{
	BNode node(cachePath);
	if (node.InitCheck() != B_OK)
		return NULL;

	BString path;
	int64 size;
	int64 modified;
	int32 cachedWidth;
	int32 cachedHeight;
	if (node.ReadAttrString(kAttrPath, &path) != B_OK || path != sourcePath
		|| !read_attribute(node, kAttrSize, B_INT64_TYPE, size)
		|| size != request.size
		|| !read_attribute(node, kAttrModified, B_INT64_TYPE, modified)
		|| modified != request.modified
		|| !read_attribute(node, kAttrWidth, B_INT32_TYPE, cachedWidth)
		|| !read_attribute(node, kAttrHeight, B_INT32_TYPE, cachedHeight)) {
		return NULL;
	}

	BBitmap* bitmap = load_bitmap(cachePath);
	if (bitmap != NULL) {
		*width = cachedWidth;
		*height = cachedHeight;
	}
	return bitmap;
}


void
ThumbnailLoader::_WriteCached(const char* cachePath, const char* sourcePath,
	const ThumbnailRequest& request, BBitmap* thumbnail, int32 width,
	int32 height)
{
	BString directory;
	directory.SetToFormat("%s/%" B_PRId32, fCacheRoot.String(),
		request.bucket);
	// create_directory() checks and then creates each path component:
	// when several workers write their first thumbnail at the same time,
	// all but one fail with B_FILE_EXISTS on a level. Try again then.
	status_t status = B_ERROR;
	for (int32 attempt = 0; attempt < 3 && status != B_OK; attempt++)
		status = create_directory(directory.String(), 0755);
	if (status != B_OK) {
		fprintf(stderr, "%s: could not create %s: %s\n", kApplicationName,
			directory.String(), strerror(status));
		return;
	}

	// write to a temporary file and rename it, so that another window (or
	// a crash) never sees a half written thumbnail
	BString tempPath;
	tempPath.SetToFormat("%s.%" B_PRId32 ".tmp", cachePath,
		find_thread(NULL));

	BFile file(tempPath.String(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (file.InitCheck() != B_OK) {
		fprintf(stderr, "%s: could not create %s: %s\n", kApplicationName,
			tempPath.String(), strerror(file.InitCheck()));
		return;
	}

	BBitmapStream stream(thumbnail);
	status = BTranslatorRoster::Default()->Translate(&stream, NULL, NULL,
		&file, B_PNG_FORMAT);
	BBitmap* detached;
	stream.DetachBitmap(&detached);
		// the thumbnail stays ours

	if (status == B_OK) {
		BString path(sourcePath);
		int64 size = request.size;
		int64 modified = request.modified;
		file.WriteAttrString(kAttrPath, &path);
		file.WriteAttr(kAttrSize, B_INT64_TYPE, 0, &size, sizeof(size));
		file.WriteAttr(kAttrModified, B_INT64_TYPE, 0, &modified,
			sizeof(modified));
		file.WriteAttr(kAttrWidth, B_INT32_TYPE, 0, &width, sizeof(width));
		file.WriteAttr(kAttrHeight, B_INT32_TYPE, 0, &height,
			sizeof(height));
		BNodeInfo(&file).SetType("image/png");
	}
	file.Unset();

	if (status == B_OK && rename(tempPath.String(), cachePath) != 0)
		status = errno;
	if (status != B_OK) {
		unlink(tempPath.String());
		fprintf(stderr, "%s: could not cache the thumbnail of %s: %s\n",
			kApplicationName, sourcePath, strerror(status));
	}
}
