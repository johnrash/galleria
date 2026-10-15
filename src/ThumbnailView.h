/*
 * Copyright 2026, Galleria authors.
 * Distributed under the terms of the MIT License.
 */
#ifndef THUMBNAIL_VIEW_H
#define THUMBNAIL_VIEW_H


#include <map>
#include <unordered_map>
#include <vector>

#include <String.h>
#include <View.h>

#include "ImageFile.h"


class BBitmap;
class ThumbnailLoader;


// The grid of thumbnails. Thumbnails are requested lazily for the visible
// rows (and a few screens around them) and dropped again far away from the
// visible area when they use too much memory.
class ThumbnailView : public BView {
public:
								ThumbnailView(const char* name);
	virtual						~ThumbnailView();

	virtual	void				AttachedToWindow();
	virtual	void				DetachedFromWindow();
	virtual	void				Draw(BRect updateRect);
	virtual	void				FrameResized(float width, float height);
	virtual	void				KeyDown(const char* bytes, int32 numBytes);
	virtual	void				MakeFocus(bool focus = true);
	virtual	void				MessageReceived(BMessage* message);
	virtual	void				MouseDown(BPoint where);
	virtual	void				WindowActivated(bool active);

			using BView::ScrollTo;
	virtual	void				ScrollTo(BPoint where);

			// Takes the files; sameFolder keeps thumbnails, selection and
			// scroll position of unchanged files (used after a rescan).
			void				SetFiles(FileList& files, bool sameFolder,
									const node_ref* select = NULL);
			void				SetEmptyText(const char* text);
			void				SetSorting(int32 sortKey, bool descending);
			void				SetThumbnailSize(int32 size);
			int32				ThumbnailSize() const
									{ return fThumbnailSize; }

			int32				CountItems() const
									{ return (int32)fItems.size(); }
			int32				CountImages() const;
			const FileItem*		ItemAt(int32 index) const;
			bool				GetImageSize(int32 index, int32* width,
									int32* height) const;

			int32				CurrentSelection() const
									{ return fSelected; }
			void				Select(int32 index);
			bool				SelectRef(const entry_ref& ref);
			void				ScrollToItem(int32 index);

			// Adds the refs of all images in display order to "refs";
			// *imageIndex is the position of the item at index among them.
			void				AddImageRefs(BMessage& message, int32 index,
									int32* imageIndex) const;

			int32				CountLoadedThumbnails() const;
			int32				CountPendingThumbnails() const;
			// images in the visible area, and how many of them are done
			void				CountVisibleThumbnails(int32* visible,
									int32* done) const;

private:
			struct Item {
				FileItem		file;
				int32			id;
				int32			index;
				BBitmap*		bitmap;
				int32			bucket;
				int32			width;
				int32			height;
				bool			failed;
			};

			void				_DeleteItem(Item* item);
			void				_SetBitmap(Item* item, BBitmap* bitmap,
									int32 bucket);
			void				_Sort();

			void				_UpdateGeometry();
			void				_UpdateScrollBar();
			float				_RowHeight() const;
			BRect				_ItemFrame(int32 index) const;
			int32				_IndexAt(BPoint point) const;
			void				_VisibleRange(int32& first,
									int32& last) const;
			int32				_AnchorIndex(float* offset) const;
			void				_RestoreAnchor(int32 index, float offset);
			void				_InvalidateItem(int32 index);

			void				_DrawItem(int32 index, BRect frame);
			BBitmap*			_Icon(const char* mimeType, int32 size,
									bool dimmed);
			void				_ClearIcons();

			void				_ScheduleRequestUpdate();
			void				_UpdateRequests();
			void				_HandleThumbnail(BMessage* message);

			void				_MoveSelection(int32 delta);
			void				_Invoke(int32 index, bool newWindow);
			void				_ShowContextMenu(int32 index, BPoint where);
			void				_NotifySelection();

private:
			std::vector<Item*>	fItems;
			std::unordered_map<int32, Item*> fItemsById;
			int32				fNextId;
			int32				fSelected;
			int32				fLastClickIndex;

			int32				fThumbnailSize;
			int32				fSortKey;
			bool				fSortDescending;

			int32				fColumns;
			float				fCellWidth;
			float				fCellHeight;
			float				fHorizontalGap;
			float				fDataHeight;
			float				fLabelHeight;
			float				fLabelAscent;

			ThumbnailLoader*	fLoader;
			bool				fRequestUpdatePending;
			size_t				fBitmapBytes;
			std::map<BString, BBitmap*> fIcons;
			BString				fEmptyText;
};


#endif	// THUMBNAIL_VIEW_H
