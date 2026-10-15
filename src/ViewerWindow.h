/*
 * Copyright 2026, Galleria authors.
 * Distributed under the terms of the MIT License.
 */
#ifndef VIEWER_WINDOW_H
#define VIEWER_WINDOW_H


#include <deque>
#include <vector>

#include <Entry.h>
#include <Messenger.h>

#include "ImageLoader.h"
#include "ZoomableWindow.h"


class BMenuBar;
class BMenuItem;
class BScrollBar;
class ImageView;


class ViewerWindow : public ZoomableWindow {
public:
								ViewerWindow(BRect frame,
									const BMessage* openMessage);
	virtual						~ViewerWindow();

	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();

			// kMsgOpenViewer: "refs", "index", optional "browser"
			// messenger and "scanFolder" (navigate the whole folder of a
			// single image).
			void				SetImages(const BMessage* message);

private:
			BMenuBar*			_CreateMenuBar();
			void				_ShowImage(int32 index);
			void				_Display(const LoadedImagePtr& image);
			void				_ImageLoaded(BMessage* message);
			void				_UpdateTitle();
			void				_UpdateMenus();
			void				_ZoomChanged();
			void				_SetFullScreen(bool fullScreen);
			void				_ShowInBrowser();

			void				_AddToCache(const LoadedImagePtr& image);
			LoadedImagePtr		_FindCached(const entry_ref& ref) const;
			void				_Prefetch();

			void				_ScanFolder(const entry_ref& ref);
			void				_FolderScanned(BMessage* message);
			void				_ReplyState(BMessage* message);

private:
			ImageView*			fImageView;
			BMenuBar*			fMenuBar;
			BScrollBar*			fHorizontalScrollBar;
			BScrollBar*			fVerticalScrollBar;

			BMenuItem*			fPreviousItem;
			BMenuItem*			fNextItem;
			BMenuItem*			fFirstItem;
			BMenuItem*			fLastItem;
			BMenuItem*			fOriginalItem;
			BMenuItem*			fFitWindowItem;
			BMenuItem*			fFitWidthItem;
			BMenuItem*			fEnlargeItem;
			BMenuItem*			fFullScreenItem;

			ImageLoader*		fLoader;
			std::vector<entry_ref> fRefs;
			int32				fIndex;
			int32				fDirection;
			bool				fLoading;
			std::deque<LoadedImagePtr> fCache;
			BMessenger			fBrowser;
			uint32				fScanGeneration;

			bool				fFullScreen;
			BRect				fWindowedFrame;
};


#endif	// VIEWER_WINDOW_H
