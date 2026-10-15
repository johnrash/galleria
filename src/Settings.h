/*
 * Copyright 2026, Galleria authors.
 * Distributed under the terms of the MIT License.
 */
#ifndef SETTINGS_H
#define SETTINGS_H


#include <Rect.h>
#include <String.h>


enum {
	kSortByName = 0,
	kSortByModified,
	kSortByCreated,
	kSortBySize,
	kSortByType,
	kSortKeyCount
};

enum {
	kZoomFitWindow = 0,
	kZoomFitWidth,
	kZoomFixed
};


// Application wide settings, shared by all windows (each window runs in its
// own thread, so every access goes through a lock). Stored as a flattened
// BMessage in ~/config/settings/Galleria settings.
class Settings {
public:
	static	void				Load();
	static	void				Save();

	// The frames are the windows' normal (unzoomed) frames; "zoomed" says
	// whether the window was maximized with the zoom button.
	static	BRect				BrowserFrame();
	static	bool				BrowserZoomed();
	static	void				SetBrowserFrame(BRect frame, bool zoomed);
	static	BRect				ViewerFrame();
	static	bool				ViewerZoomed();
	static	void				SetViewerFrame(BRect frame, bool zoomed);

	static	BString				LastFolder();
	static	void				SetLastFolder(const char* path);

	static	int32				ThumbnailSize();
	static	void				SetThumbnailSize(int32 size);

	static	int32				SortKey();
	static	bool				SortDescending();
	static	void				SetSorting(int32 key, bool descending);

	static	bool				ShowFolders();
	static	void				SetShowFolders(bool show);

	// The zoom is shared by all images: whatever was chosen last is used for
	// the next image too, and survives restarts.
	static	int32				ZoomMode();
	static	float				ZoomFactor();
	static	void				SetZoom(int32 mode, float factor);

	static	bool				EnlargeSmallImages();
	static	void				SetEnlargeSmallImages(bool enlarge);
};


#endif	// SETTINGS_H
