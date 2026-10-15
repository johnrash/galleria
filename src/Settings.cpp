/*
 * Copyright 2026, Galleria authors.
 * Distributed under the terms of the MIT License.
 */


#include "Settings.h"

#include <Autolock.h>
#include <File.h>
#include <FindDirectory.h>
#include <Locker.h>
#include <Message.h>
#include <Path.h>

#include "Galleria.h"


static const char* kSettingsFileName = "Galleria settings";


namespace {

struct SettingsData {
	BRect		browserFrame;
	bool		browserZoomed;
	BRect		viewerFrame;
	bool		viewerZoomed;
	BString		lastFolder;
	int32		thumbnailSize;
	int32		sortKey;
	bool		sortDescending;
	bool		showFolders;
	int32		zoomMode;
	float		zoomFactor;
	bool		enlargeSmall;

	SettingsData()
		:
		browserFrame(),
		browserZoomed(false),
		viewerFrame(),
		viewerZoomed(false),
		thumbnailSize(kDefaultThumbnailSize),
		sortKey(kSortByName),
		sortDescending(false),
		showFolders(true),
		zoomMode(kZoomFitWindow),
		zoomFactor(1.0f),
		enlargeSmall(false)
	{
	}
};

BLocker sLock("Galleria settings");
SettingsData sData;


status_t
settings_path(BPath& path)
{
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &path, true);
	if (status != B_OK)
		return status;
	return path.Append(kSettingsFileName);
}

}	// namespace


void
Settings::Load()
{
	BPath path;
	if (settings_path(path) != B_OK)
		return;

	BFile file(path.Path(), B_READ_ONLY);
	BMessage archive;
	if (file.InitCheck() != B_OK || archive.Unflatten(&file) != B_OK)
		return;

	BAutolock _(sLock);
	SettingsData defaults;
	sData.browserFrame = archive.GetRect("browserFrame", BRect());
	sData.browserZoomed = archive.GetBool("browserZoomed", false);
	sData.viewerFrame = archive.GetRect("viewerFrame", BRect());
	sData.viewerZoomed = archive.GetBool("viewerZoomed", false);
	sData.lastFolder = archive.GetString("lastFolder", "");
	sData.thumbnailSize = archive.GetInt32("thumbnailSize",
		defaults.thumbnailSize);
	sData.sortKey = archive.GetInt32("sortKey", defaults.sortKey);
	sData.sortDescending = archive.GetBool("sortDescending", false);
	sData.showFolders = archive.GetBool("showFolders", true);
	sData.zoomMode = archive.GetInt32("zoomMode", defaults.zoomMode);
	sData.zoomFactor = archive.GetFloat("zoomFactor", defaults.zoomFactor);
	sData.enlargeSmall = archive.GetBool("enlargeSmall", false);

	// sanitize
	if (sData.thumbnailSize < kMinThumbnailSize)
		sData.thumbnailSize = kMinThumbnailSize;
	if (sData.thumbnailSize > kMaxThumbnailSize)
		sData.thumbnailSize = kMaxThumbnailSize;
	if (sData.sortKey < 0 || sData.sortKey >= kSortKeyCount)
		sData.sortKey = kSortByName;
	if (sData.zoomMode < kZoomFitWindow || sData.zoomMode > kZoomFixed)
		sData.zoomMode = kZoomFitWindow;
	if (!(sData.zoomFactor > 0.0f))
		sData.zoomFactor = 1.0f;
}


void
Settings::Save()
{
	BMessage archive;
	{
		BAutolock _(sLock);
		archive.AddRect("browserFrame", sData.browserFrame);
		archive.AddBool("browserZoomed", sData.browserZoomed);
		archive.AddRect("viewerFrame", sData.viewerFrame);
		archive.AddBool("viewerZoomed", sData.viewerZoomed);
		archive.AddString("lastFolder", sData.lastFolder);
		archive.AddInt32("thumbnailSize", sData.thumbnailSize);
		archive.AddInt32("sortKey", sData.sortKey);
		archive.AddBool("sortDescending", sData.sortDescending);
		archive.AddBool("showFolders", sData.showFolders);
		archive.AddInt32("zoomMode", sData.zoomMode);
		archive.AddFloat("zoomFactor", sData.zoomFactor);
		archive.AddBool("enlargeSmall", sData.enlargeSmall);
	}

	BPath path;
	if (settings_path(path) != B_OK)
		return;

	BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (file.InitCheck() == B_OK)
		archive.Flatten(&file);
}


BRect
Settings::BrowserFrame()
{
	BAutolock _(sLock);
	return sData.browserFrame;
}


bool
Settings::BrowserZoomed()
{
	BAutolock _(sLock);
	return sData.browserZoomed;
}


void
Settings::SetBrowserFrame(BRect frame, bool zoomed)
{
	BAutolock _(sLock);
	sData.browserFrame = frame;
	sData.browserZoomed = zoomed;
}


BRect
Settings::ViewerFrame()
{
	BAutolock _(sLock);
	return sData.viewerFrame;
}


bool
Settings::ViewerZoomed()
{
	BAutolock _(sLock);
	return sData.viewerZoomed;
}


void
Settings::SetViewerFrame(BRect frame, bool zoomed)
{
	BAutolock _(sLock);
	sData.viewerFrame = frame;
	sData.viewerZoomed = zoomed;
}


BString
Settings::LastFolder()
{
	BAutolock _(sLock);
	return sData.lastFolder;
}


void
Settings::SetLastFolder(const char* path)
{
	BAutolock _(sLock);
	sData.lastFolder = path;
}


int32
Settings::ThumbnailSize()
{
	BAutolock _(sLock);
	return sData.thumbnailSize;
}


void
Settings::SetThumbnailSize(int32 size)
{
	BAutolock _(sLock);
	sData.thumbnailSize = size;
}


int32
Settings::SortKey()
{
	BAutolock _(sLock);
	return sData.sortKey;
}


bool
Settings::SortDescending()
{
	BAutolock _(sLock);
	return sData.sortDescending;
}


void
Settings::SetSorting(int32 key, bool descending)
{
	BAutolock _(sLock);
	sData.sortKey = key;
	sData.sortDescending = descending;
}


bool
Settings::ShowFolders()
{
	BAutolock _(sLock);
	return sData.showFolders;
}


void
Settings::SetShowFolders(bool show)
{
	BAutolock _(sLock);
	sData.showFolders = show;
}


int32
Settings::ZoomMode()
{
	BAutolock _(sLock);
	return sData.zoomMode;
}


float
Settings::ZoomFactor()
{
	BAutolock _(sLock);
	return sData.zoomFactor;
}


void
Settings::SetZoom(int32 mode, float factor)
{
	BAutolock _(sLock);
	sData.zoomMode = mode;
	if (factor > 0.0f)
		sData.zoomFactor = factor;
}


bool
Settings::EnlargeSmallImages()
{
	BAutolock _(sLock);
	return sData.enlargeSmall;
}


void
Settings::SetEnlargeSmallImages(bool enlarge)
{
	BAutolock _(sLock);
	sData.enlargeSmall = enlarge;
}
