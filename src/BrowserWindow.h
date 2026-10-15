/*
 * Copyright 2026, Galleria authors.
 * Distributed under the terms of the MIT License.
 */
#ifndef BROWSER_WINDOW_H
#define BROWSER_WINDOW_H


#include <Entry.h>
#include <Node.h>

#include "ZoomableWindow.h"


class BButton;
class BFilePanel;
class BMenu;
class BMenuBar;
class BMenuField;
class BMenuItem;
class BMessageRunner;
class BSlider;
class BStringView;
class BTextControl;
class ThumbnailView;


class BrowserWindow : public ZoomableWindow {
public:
								BrowserWindow(BRect frame,
									const entry_ref& folder,
									const entry_ref* select = NULL);
	virtual						~BrowserWindow();

	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();

			void				SetFolder(const entry_ref& folder,
									const node_ref* select = NULL);

private:
			BMenuBar*			_CreateMenuBar();
			void				_AddSortItems(BMenu* menu);
			void				_AddOrderItems(BMenu* menu);
			void				_UpdateSortMarks();
			void				_ApplySorting(int32 key, bool descending);
			void				_SetThumbnailSize(int32 size);

			void				_StartScan(bool sameFolder);
			void				_ScanDone(BMessage* message);
			void				_UpdateStatus();

			void				_GoToParent();
			void				_GoToPath(const char* path);
			void				_GoToRef(const entry_ref& ref);
			void				_OpenItem(int32 index, bool newWindow);
			void				_ShowInTracker(int32 index);
			void				_ClearCache();
			void				_ReplyState(BMessage* message);

private:
			ThumbnailView*		fView;
			BButton*			fUpButton;
			BTextControl*		fLocation;
			BMenuField*			fSortField;
			BMenuField*			fOrderField;
			BSlider*			fSizeSlider;
			BStringView*		fStatus;

			BMenu*				fSortMenu;
			BMenu*				fOrderMenu;
			BMenuItem*			fParentItem;
			BMenuItem*			fShowFoldersItem;

			entry_ref			fFolder;
			node_ref			fFolderNode;
			uint32				fScanGeneration;
			bool				fScanning;
			bool				fScanSameFolder;
			node_ref			fPendingSelect;
			bool				fHasPendingSelect;
			BMessageRunner*		fRescanRunner;
			BFilePanel*			fFolderPanel;
};


#endif	// BROWSER_WINDOW_H
