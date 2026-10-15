/*
 * Copyright 2026, Galleria authors.
 * Distributed under the terms of the MIT License.
 */


#include "ThumbnailView.h"

#include <algorithm>

#include <math.h>

#include <Bitmap.h>
#include <Collator.h>
#include <IconUtils.h>
#include <MenuItem.h>
#include <MimeType.h>
#include <PopUpMenu.h>
#include <ScrollBar.h>
#include <Window.h>

#include "Galleria.h"
#include "Settings.h"
#include "ThumbnailLoader.h"


static const float kSpacing = 8.0f;
	// between cells and around the grid
static const float kPadding = 6.0f;
	// inside a cell, around the thumbnail
static const float kLabelGap = 3.0f;
static const size_t kMaxBitmapBytes = 192 * 1024 * 1024;

static const char* kFolderType = "application/x-vnd.Be-directory";
static const char* kImageType = "image";


ThumbnailView::ThumbnailView(const char* name)
	:
	BView(name, B_WILL_DRAW | B_FRAME_EVENTS | B_NAVIGABLE),
	fNextId(1),
	fSelected(-1),
	fLastClickIndex(-1),
	fThumbnailSize(Settings::ThumbnailSize()),
	fSortKey(Settings::SortKey()),
	fSortDescending(Settings::SortDescending()),
	fColumns(1),
	fCellWidth(0),
	fCellHeight(0),
	fHorizontalGap(kSpacing),
	fDataHeight(0),
	fLabelHeight(0),
	fLabelAscent(0),
	fLoader(NULL),
	fRequestUpdatePending(false),
	fBitmapBytes(0)
{
	// we paint the whole background ourselves (no flicker)
	SetViewColor(B_TRANSPARENT_COLOR);

	// Without an explicit minimum, BView uses its current size as its
	// minimum, and with B_AUTO_UPDATE_SIZE_LIMITS the window could then
	// never become smaller than it once was (after zooming, for example).
	SetExplicitMinSize(BSize(100, 100));
}


ThumbnailView::~ThumbnailView()
{
	delete fLoader;
	for (size_t i = 0; i < fItems.size(); i++) {
		delete fItems[i]->bitmap;
		delete fItems[i];
	}
	_ClearIcons();
}


void
ThumbnailView::AttachedToWindow()
{
	BView::AttachedToWindow();
	if (fLoader == NULL)
		fLoader = new ThumbnailLoader(BMessenger(this));
	_UpdateGeometry();
	_UpdateScrollBar();
}


void
ThumbnailView::DetachedFromWindow()
{
	// stop the workers while the window still exists
	delete fLoader;
	fLoader = NULL;
	BView::DetachedFromWindow();
}


void
ThumbnailView::Draw(BRect updateRect)
{
	rgb_color background = ui_color(B_LIST_BACKGROUND_COLOR);
	SetLowColor(background);
	FillRect(updateRect, B_SOLID_LOW);

	if (fItems.empty()) {
		if (!fEmptyText.IsEmpty()) {
			BRect bounds = Bounds();
			SetHighColor(tint_color(ui_color(B_LIST_ITEM_TEXT_COLOR),
				B_LIGHTEN_1_TINT));
			SetDrawingMode(B_OP_OVER);
			float width = StringWidth(fEmptyText.String());
			DrawString(fEmptyText.String(),
				BPoint(bounds.left + floorf((bounds.Width() - width) / 2),
					bounds.top + 40));
			SetDrawingMode(B_OP_COPY);
		}
		return;
	}

	float rowHeight = _RowHeight();
	int32 firstRow = std::max((int32)0,
		(int32)((updateRect.top - kSpacing) / rowHeight));
	int32 lastRow = (int32)((updateRect.bottom - kSpacing) / rowHeight);
	int32 count = CountItems();

	for (int32 row = firstRow; row <= lastRow; row++) {
		for (int32 column = 0; column < fColumns; column++) {
			int32 index = row * fColumns + column;
			if (index >= count)
				return;
			BRect frame = _ItemFrame(index);
			if (frame.Intersects(updateRect))
				_DrawItem(index, frame);
		}
	}
}


void
ThumbnailView::FrameResized(float width, float height)
{
	float offset;
	int32 anchor = _AnchorIndex(&offset);
	int32 oldColumns = fColumns;

	_UpdateGeometry();
	_UpdateScrollBar();
	if (fColumns != oldColumns) {
		_RestoreAnchor(anchor, offset);
		Invalidate();
	}
	_ScheduleRequestUpdate();
}


void
ThumbnailView::KeyDown(const char* bytes, int32 numBytes)
{
	int32 count = CountItems();
	int32 rowsPerPage = std::max((int32)1,
		(int32)((Bounds().Height() + 1) / _RowHeight()));

	switch (bytes[0]) {
		case B_LEFT_ARROW:
			_MoveSelection(-1);
			break;
		case B_RIGHT_ARROW:
			_MoveSelection(1);
			break;
		case B_UP_ARROW:
			_MoveSelection(-fColumns);
			break;
		case B_DOWN_ARROW:
			_MoveSelection(fColumns);
			break;
		case B_PAGE_UP:
			_MoveSelection(-fColumns * rowsPerPage);
			break;
		case B_PAGE_DOWN:
			_MoveSelection(fColumns * rowsPerPage);
			break;
		case B_HOME:
			if (count > 0) {
				Select(0);
				ScrollToItem(0);
			}
			break;
		case B_END:
			if (count > 0) {
				Select(count - 1);
				ScrollToItem(count - 1);
			}
			break;
		case B_ENTER:
			if (fSelected >= 0)
				_Invoke(fSelected, false);
			break;
		case B_BACKSPACE:
			Window()->PostMessage(kMsgParentFolder);
			break;
		default:
			BView::KeyDown(bytes, numBytes);
			break;
	}
}


void
ThumbnailView::MakeFocus(bool focus)
{
	BView::MakeFocus(focus);
	_InvalidateItem(fSelected);
}


void
ThumbnailView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgThumbnailReady:
			_HandleThumbnail(message);
			break;

		case kMsgUpdateRequests:
			fRequestUpdatePending = false;
			_UpdateRequests();
			break;

		case B_MOUSE_WHEEL_CHANGED:
		{
			// Ctrl (or Command) + wheel changes the thumbnail size
			if ((modifiers() & (B_CONTROL_KEY | B_COMMAND_KEY)) != 0) {
				float delta = message->GetFloat("be:wheel_delta_y", 0.0f);
				if (delta != 0.0f) {
					BMessage step(kMsgThumbSizeStep);
					step.AddInt32("delta", delta < 0 ? 1 : -1);
					Window()->PostMessage(&step);
				}
				break;
			}
			BView::MessageReceived(message);
			break;
		}

		default:
			BView::MessageReceived(message);
			break;
	}
}


void
ThumbnailView::MouseDown(BPoint where)
{
	MakeFocus(true);

	int32 buttons = 0;
	int32 clicks = 0;
	BMessage* current = Window()->CurrentMessage();
	if (current != NULL) {
		current->FindInt32("buttons", &buttons);
		current->FindInt32("clicks", &clicks);
	}

	int32 index = _IndexAt(where);

	if ((buttons & B_SECONDARY_MOUSE_BUTTON) != 0) {
		if (index >= 0) {
			Select(index);
			_ShowContextMenu(index, where);
		}
		return;
	}

	if (index < 0) {
		Select(-1);
		fLastClickIndex = -1;
		return;
	}

	bool doubleClick = clicks >= 2 && index == fLastClickIndex;
	Select(index);
	fLastClickIndex = index;
	if (doubleClick)
		_Invoke(index, false);
}


void
ThumbnailView::WindowActivated(bool active)
{
	BView::WindowActivated(active);
	_InvalidateItem(fSelected);
}


void
ThumbnailView::ScrollTo(BPoint where)
{
	BView::ScrollTo(where);
	_ScheduleRequestUpdate();
}


void
ThumbnailView::SetFiles(FileList& files, bool sameFolder,
	const node_ref* select)
{
	typedef std::map<std::pair<dev_t, ino_t>, Item*> ItemMap;
	ItemMap previous;
	for (size_t i = 0; i < fItems.size(); i++) {
		Item* item = fItems[i];
		previous[std::make_pair(item->file.nodeRef.device,
			item->file.nodeRef.node)] = item;
	}

	node_ref selectedNode;
	bool keepSelection = false;
	if (select != NULL) {
		selectedNode = *select;
		keepSelection = true;
	} else if (sameFolder && fSelected >= 0) {
		selectedNode = fItems[fSelected]->file.nodeRef;
		keepSelection = true;
	}

	std::vector<Item*> items;
	items.reserve(files.size());
	for (size_t i = 0; i < files.size(); i++) {
		const FileItem& file = files[i];
		Item* item = NULL;
		ItemMap::iterator found = previous.find(
			std::make_pair(file.nodeRef.device, file.nodeRef.node));
		if (sameFolder && found != previous.end()
			&& found->second->file.modified == file.modified
			&& found->second->file.size == file.size) {
			// unchanged: keep the thumbnail
			item = found->second;
			previous.erase(found);
			item->file = file;
		} else {
			item = new Item;
			item->file = file;
			item->id = fNextId++;
			item->index = 0;
			item->bitmap = NULL;
			item->bucket = 0;
			item->width = 0;
			item->height = 0;
			item->failed = false;
			fItemsById[item->id] = item;
		}
		items.push_back(item);
	}

	for (ItemMap::iterator it = previous.begin(); it != previous.end(); it++)
		_DeleteItem(it->second);

	fItems.swap(items);
	_Sort();

	fSelected = -1;
	fLastClickIndex = -1;
	if (keepSelection) {
		for (size_t i = 0; i < fItems.size(); i++) {
			if (fItems[i]->file.nodeRef == selectedNode) {
				fSelected = (int32)i;
				break;
			}
		}
	}

	_UpdateGeometry();
	_UpdateScrollBar();
	if (!sameFolder) {
		ScrollTo(0, 0);
		if (fSelected >= 0)
			ScrollToItem(fSelected);
	}

	Invalidate();
	_ScheduleRequestUpdate();
	_NotifySelection();
}


void
ThumbnailView::SetEmptyText(const char* text)
{
	if (fEmptyText == text)
		return;
	fEmptyText = text;
	if (fItems.empty())
		Invalidate();
}


void
ThumbnailView::SetSorting(int32 sortKey, bool descending)
{
	if (sortKey == fSortKey && descending == fSortDescending)
		return;

	Item* selected = fSelected >= 0 ? fItems[fSelected] : NULL;
	fSortKey = sortKey;
	fSortDescending = descending;
	_Sort();

	if (selected != NULL) {
		fSelected = selected->index;
		ScrollToItem(fSelected);
	} else
		ScrollTo(0, 0);

	Invalidate();
	_ScheduleRequestUpdate();
}


void
ThumbnailView::SetThumbnailSize(int32 size)
{
	size = std::max(kMinThumbnailSize, std::min(kMaxThumbnailSize, size));
	if (size == fThumbnailSize)
		return;

	float offset;
	int32 anchor = _AnchorIndex(&offset);

	fThumbnailSize = size;
	_ClearIcons();
	_UpdateGeometry();
	_UpdateScrollBar();
	_RestoreAnchor(anchor, offset);
	Invalidate();
	_ScheduleRequestUpdate();
}


int32
ThumbnailView::CountImages() const
{
	int32 count = 0;
	for (size_t i = 0; i < fItems.size(); i++) {
		if (!fItems[i]->file.isFolder)
			count++;
	}
	return count;
}


const FileItem*
ThumbnailView::ItemAt(int32 index) const
{
	if (index < 0 || index >= CountItems())
		return NULL;
	return &fItems[index]->file;
}


bool
ThumbnailView::GetImageSize(int32 index, int32* width, int32* height) const
{
	if (index < 0 || index >= CountItems() || fItems[index]->width <= 0)
		return false;
	*width = fItems[index]->width;
	*height = fItems[index]->height;
	return true;
}


void
ThumbnailView::Select(int32 index)
{
	if (index < -1 || index >= CountItems())
		index = -1;
	if (index == fSelected)
		return;

	_InvalidateItem(fSelected);
	fSelected = index;
	_InvalidateItem(fSelected);
	_NotifySelection();
}


bool
ThumbnailView::SelectRef(const entry_ref& ref)
{
	for (size_t i = 0; i < fItems.size(); i++) {
		if (fItems[i]->file.ref == ref) {
			Select((int32)i);
			ScrollToItem((int32)i);
			return true;
		}
	}
	return false;
}


void
ThumbnailView::ScrollToItem(int32 index)
{
	if (index < 0 || index >= CountItems())
		return;

	BRect frame = _ItemFrame(index);
	BRect bounds = Bounds();
	if (frame.top - kSpacing < bounds.top)
		ScrollTo(0, std::max(0.0f, frame.top - kSpacing));
	else if (frame.bottom + kSpacing > bounds.bottom) {
		ScrollTo(0, std::max(0.0f,
			frame.bottom + kSpacing - bounds.Height()));
	}
}


void
ThumbnailView::AddImageRefs(BMessage& message, int32 index,
	int32* imageIndex) const
{
	int32 count = 0;
	*imageIndex = 0;
	for (size_t i = 0; i < fItems.size(); i++) {
		if (fItems[i]->file.isFolder)
			continue;
		if ((int32)i == index)
			*imageIndex = count;
		message.AddRef("refs", &fItems[i]->file.ref);
		count++;
	}
}


int32
ThumbnailView::CountLoadedThumbnails() const
{
	int32 bucket = ThumbnailLoader::BucketFor(fThumbnailSize);
	int32 count = 0;
	for (size_t i = 0; i < fItems.size(); i++) {
		if (fItems[i]->bitmap != NULL && fItems[i]->bucket == bucket)
			count++;
	}
	return count;
}


int32
ThumbnailView::CountPendingThumbnails() const
{
	return fLoader != NULL ? fLoader->PendingCount() : 0;
}


void
ThumbnailView::CountVisibleThumbnails(int32* visible, int32* done) const
{
	*visible = 0;
	*done = 0;
	if (fItems.empty())
		return;

	int32 bucket = ThumbnailLoader::BucketFor(fThumbnailSize);
	int32 first, last;
	_VisibleRange(first, last);
	for (int32 i = first; i <= last; i++) {
		const Item* item = fItems[i];
		if (item->file.isFolder)
			continue;
		(*visible)++;
		if (item->failed || (item->bitmap != NULL && item->bucket == bucket))
			(*done)++;
	}
}


// #pragma mark - private


void
ThumbnailView::_DeleteItem(Item* item)
{
	_SetBitmap(item, NULL, 0);
	fItemsById.erase(item->id);
	delete item;
}


void
ThumbnailView::_SetBitmap(Item* item, BBitmap* bitmap, int32 bucket)
{
	if (item->bitmap != NULL) {
		fBitmapBytes -= item->bitmap->BitsLength();
		delete item->bitmap;
	}
	item->bitmap = bitmap;
	item->bucket = bucket;
	if (bitmap != NULL)
		fBitmapBytes += bitmap->BitsLength();
}


void
ThumbnailView::_Sort()
{
	BCollator collator;
	init_name_collator(collator);
	int32 sortKey = fSortKey;
	bool descending = fSortDescending;
	std::stable_sort(fItems.begin(), fItems.end(),
		[&](const Item* a, const Item* b) {
			return compare_files(a->file, b->file, sortKey, descending,
				collator) < 0;
		});
	for (size_t i = 0; i < fItems.size(); i++)
		fItems[i]->index = (int32)i;
}


void
ThumbnailView::_UpdateGeometry()
{
	font_height fontHeight;
	GetFontHeight(&fontHeight);
	fLabelAscent = ceilf(fontHeight.ascent);
	fLabelHeight = ceilf(fontHeight.ascent + fontHeight.descent);

	fCellWidth = fThumbnailSize + 2 * kPadding;
	fCellHeight = fThumbnailSize + 2 * kPadding + kLabelGap + fLabelHeight;

	float width = Bounds().Width() + 1;
	fColumns = std::max((int32)1,
		(int32)((width - kSpacing) / (fCellWidth + kSpacing)));
	fHorizontalGap = std::max(kSpacing,
		floorf((width - fColumns * fCellWidth) / (fColumns + 1)));

	int32 rows = (CountItems() + fColumns - 1) / fColumns;
	fDataHeight = kSpacing + rows * _RowHeight();
}


void
ThumbnailView::_UpdateScrollBar()
{
	BScrollBar* scrollBar = ScrollBar(B_VERTICAL);
	if (scrollBar == NULL)
		return;

	float visible = Bounds().Height() + 1;
	float maximum = std::max(0.0f, fDataHeight - visible);
	scrollBar->SetRange(0, maximum);
	scrollBar->SetProportion(fDataHeight > visible
		? visible / fDataHeight : 1.0f);

	// one wheel notch (three small steps) scrolls one row
	float rowHeight = _RowHeight();
	scrollBar->SetSteps(ceilf(rowHeight / 3),
		std::max(rowHeight, visible - rowHeight));
}


float
ThumbnailView::_RowHeight() const
{
	return fCellHeight + kSpacing;
}


BRect
ThumbnailView::_ItemFrame(int32 index) const
{
	int32 row = index / fColumns;
	int32 column = index % fColumns;
	float left = floorf(fHorizontalGap
		+ column * (fCellWidth + fHorizontalGap));
	float top = kSpacing + row * _RowHeight();
	return BRect(left, top, left + fCellWidth - 1, top + fCellHeight - 1);
}


int32
ThumbnailView::_IndexAt(BPoint point) const
{
	if (point.y < kSpacing || point.x < fHorizontalGap)
		return -1;

	int32 row = (int32)((point.y - kSpacing) / _RowHeight());
	int32 column = (int32)((point.x - fHorizontalGap)
		/ (fCellWidth + fHorizontalGap));
	if (column >= fColumns)
		return -1;

	int32 index = row * fColumns + column;
	if (index >= CountItems() || !_ItemFrame(index).Contains(point))
		return -1;
	return index;
}


void
ThumbnailView::_VisibleRange(int32& first, int32& last) const
{
	BRect bounds = Bounds();
	float rowHeight = _RowHeight();
	int32 firstRow = std::max((int32)0,
		(int32)((bounds.top - kSpacing) / rowHeight));
	int32 lastRow = std::max(firstRow,
		(int32)((bounds.bottom - kSpacing) / rowHeight));

	first = std::min(firstRow * fColumns, CountItems() - 1);
	last = std::min((lastRow + 1) * fColumns - 1, CountItems() - 1);
}


// The item that should stay in place when the layout changes: the
// selection if it is visible, otherwise the first visible item.
int32
ThumbnailView::_AnchorIndex(float* offset) const
{
	if (fItems.empty())
		return -1;

	BRect bounds = Bounds();
	int32 anchor = -1;
	if (fSelected >= 0 && _ItemFrame(fSelected).Intersects(bounds))
		anchor = fSelected;
	else {
		int32 last;
		_VisibleRange(anchor, last);
	}
	if (anchor < 0)
		return -1;

	*offset = _ItemFrame(anchor).top - bounds.top;
	return anchor;
}


void
ThumbnailView::_RestoreAnchor(int32 index, float offset)
{
	if (index < 0 || index >= CountItems())
		return;

	float maximum = std::max(0.0f, fDataHeight - (Bounds().Height() + 1));
	float top = _ItemFrame(index).top - offset;
	ScrollTo(0, std::max(0.0f, std::min(maximum, top)));
}


void
ThumbnailView::_InvalidateItem(int32 index)
{
	if (index >= 0 && index < CountItems())
		Invalidate(_ItemFrame(index));
}


void
ThumbnailView::_DrawItem(int32 index, BRect frame)
{
	Item* item = fItems[index];
	rgb_color background = ui_color(B_LIST_BACKGROUND_COLOR);
	bool selected = index == fSelected;

	if (selected) {
		rgb_color color = ui_color(B_LIST_SELECTED_BACKGROUND_COLOR);
		if (!IsFocus() || Window() == NULL || !Window()->IsActive())
			color = mix_color(color, background, 128);
		SetHighColor(color);
		FillRoundRect(frame, 5, 5);
		background = color;
	}

	float size = fThumbnailSize;
	BRect box(frame.left + kPadding, frame.top + kPadding,
		frame.left + kPadding + size - 1, frame.top + kPadding + size - 1);

	BBitmap* bitmap = item->bitmap;
	bool alpha = true;
	BRect source;
	BRect target;

	if (item->file.isFolder) {
		int32 iconSize = (int32)(size * 3 / 4);
		bitmap = _Icon(kFolderType, iconSize, false);
		target.Set(0, 0, iconSize - 1, iconSize - 1);
	} else if (bitmap != NULL) {
		BRect bounds = bitmap->Bounds();
		int32 width = item->width > 0 ? item->width : bounds.IntegerWidth() + 1;
		int32 height = item->height > 0
			? item->height : bounds.IntegerHeight() + 1;
		int32 fitWidth, fitHeight;
		fit_size(width, height, (int32)size, (int32)size, fitWidth,
			fitHeight);
		target.Set(0, 0, fitWidth - 1, fitHeight - 1);
		alpha = bitmap->ColorSpace() == B_RGBA32;
	} else {
		int32 iconSize = (int32)(size / 2);
		bitmap = _Icon(kImageType, iconSize, !item->failed);
		target.Set(0, 0, iconSize - 1, iconSize - 1);
	}

	if (bitmap != NULL) {
		target.OffsetTo(box.left + floorf((size - target.Width() - 1) / 2),
			box.top + floorf((size - target.Height() - 1) / 2));
		source = bitmap->Bounds();

		if (alpha) {
			SetDrawingMode(B_OP_ALPHA);
			SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
		} else
			SetDrawingMode(B_OP_COPY);
		DrawBitmap(bitmap, source, target, B_FILTER_BITMAP_BILINEAR);
		SetDrawingMode(B_OP_COPY);

		if (!alpha && item->bitmap != NULL) {
			// a thin frame sets photos off the background
			SetHighColor(tint_color(background, B_DARKEN_2_TINT));
			StrokeRect(target.InsetByCopy(-1, -1));
		}
	}

	// the name, shortened in the middle to fit
	BString label(item->file.name);
	BFont font;
	GetFont(&font);
	font.TruncateString(&label, B_TRUNCATE_MIDDLE, fCellWidth - 4);
	float labelWidth = font.StringWidth(label.String());

	SetHighColor(ui_color(selected
		? B_LIST_SELECTED_ITEM_TEXT_COLOR : B_LIST_ITEM_TEXT_COLOR));
	SetLowColor(background);
	SetDrawingMode(B_OP_OVER);
	DrawString(label.String(),
		BPoint(frame.left + floorf((fCellWidth - labelWidth) / 2),
			box.bottom + 1 + kLabelGap + fLabelAscent));
	SetDrawingMode(B_OP_COPY);
}


BBitmap*
ThumbnailView::_Icon(const char* mimeType, int32 size, bool dimmed)
{
	if (size < 1)
		return NULL;

	BString key;
	key.SetToFormat("%s:%" B_PRId32 ":%d", mimeType, size, dimmed ? 1 : 0);
	std::map<BString, BBitmap*>::iterator found = fIcons.find(key);
	if (found != fIcons.end())
		return found->second;

	BBitmap* icon = NULL;
	uint8* data = NULL;
	size_t dataSize = 0;
	BMimeType type(mimeType);
	if (type.GetIcon(&data, &dataSize) == B_OK) {
		icon = new BBitmap(BRect(0, 0, size - 1, size - 1), B_RGBA32);
		if (icon->InitCheck() != B_OK
			|| BIconUtils::GetVectorIcon(data, dataSize, icon) != B_OK) {
			delete icon;
			icon = NULL;
		}
		delete[] data;
	}

	if (icon != NULL && dimmed) {
		// placeholder while the thumbnail is being made
		uint8* bits = (uint8*)icon->Bits();
		int32 bytesPerRow = icon->BytesPerRow();
		for (int32 y = 0; y < size; y++) {
			uint8* pixel = bits + y * bytesPerRow;
			for (int32 x = 0; x < size; x++, pixel += 4)
				pixel[3] = pixel[3] * 2 / 5;
		}
	}

	fIcons[key] = icon;
	return icon;
}


void
ThumbnailView::_ClearIcons()
{
	for (std::map<BString, BBitmap*>::iterator it = fIcons.begin();
			it != fIcons.end(); it++) {
		delete it->second;
	}
	fIcons.clear();
}


void
ThumbnailView::_ScheduleRequestUpdate()
{
	// coalesces the many scroll and resize events into one update
	if (fRequestUpdatePending || Looper() == NULL)
		return;
	fRequestUpdatePending = true;
	Looper()->PostMessage(kMsgUpdateRequests, this);
}


void
ThumbnailView::_UpdateRequests()
{
	if (fLoader == NULL)
		return;

	std::vector<ThumbnailRequest> requests;
	int32 count = CountItems();
	if (count == 0) {
		fLoader->SetRequests(requests);
		return;
	}

	int32 bucket = ThumbnailLoader::BucketFor(fThumbnailSize);
	int32 first, last;
	_VisibleRange(first, last);
	int32 visibleCount = last - first + 1;

	auto request = [&](int32 index) {
		Item* item = fItems[index];
		if (item->file.isFolder || item->failed
			|| (item->bitmap != NULL && item->bucket == bucket)) {
			return;
		}
		ThumbnailRequest request;
		request.id = item->id;
		request.ref = item->file.ref;
		request.bucket = bucket;
		request.size = item->file.size;
		request.modified = item->file.modified;
		requests.push_back(request);
	};

	// visible items first, then two screens ahead, then one screen back
	for (int32 i = first; i <= last; i++)
		request(i);
	int32 aheadEnd = std::min(count - 1, last + 2 * visibleCount);
	for (int32 i = last + 1; i <= aheadEnd; i++)
		request(i);
	int32 behindStart = std::max((int32)0, first - visibleCount);
	for (int32 i = first - 1; i >= behindStart; i--)
		request(i);

	fLoader->SetRequests(requests);

	if (fBitmapBytes > kMaxBitmapBytes) {
		int32 keepFirst = std::max((int32)0, first - 2 * visibleCount);
		int32 keepLast = std::min(count - 1, last + 3 * visibleCount);
		for (int32 i = 0; i < count; i++) {
			if (i < keepFirst || i > keepLast)
				_SetBitmap(fItems[i], NULL, 0);
		}
	}
}


void
ThumbnailView::_HandleThumbnail(BMessage* message)
{
	int32 id = message->GetInt32("id", -1);
	int32 bucket = message->GetInt32("bucket", 0);
	BBitmap* bitmap = (BBitmap*)message->GetPointer("bitmap");

	std::unordered_map<int32, Item*>::iterator found = fItemsById.find(id);
	if (found == fItemsById.end()) {
		// the file is gone (or the folder changed)
		delete bitmap;
		return;
	}

	Item* item = found->second;
	int32 width = message->GetInt32("width", 0);
	int32 height = message->GetInt32("height", 0);
	if (width > 0 && height > 0) {
		item->width = width;
		item->height = height;
	}

	int32 currentBucket = ThumbnailLoader::BucketFor(fThumbnailSize);
	if (bitmap == NULL) {
		// keep a smaller thumbnail if there is one, but do not try again
		item->failed = true;
	} else if (item->bitmap != NULL && item->bucket == currentBucket
		&& bucket != currentBucket) {
		// we already have a better one
		delete bitmap;
		return;
	} else
		_SetBitmap(item, bitmap, bucket);

	_InvalidateItem(item->index);
	if (bitmap != NULL && bucket != currentBucket)
		_ScheduleRequestUpdate();
	if (item->index == fSelected)
		_NotifySelection();
}


void
ThumbnailView::_MoveSelection(int32 delta)
{
	int32 count = CountItems();
	if (count == 0)
		return;

	int32 index;
	if (fSelected < 0) {
		int32 last;
		_VisibleRange(index, last);
		index = std::max((int32)0, index);
	} else
		index = std::max((int32)0, std::min(count - 1, fSelected + delta));

	Select(index);
	ScrollToItem(index);
}


void
ThumbnailView::_Invoke(int32 index, bool newWindow)
{
	BMessage message(kMsgOpenItem);
	message.AddInt32("index", index);
	message.AddBool("newWindow", newWindow);
	Window()->PostMessage(&message);
}


void
ThumbnailView::_ShowContextMenu(int32 index, BPoint where)
{
	const FileItem& file = fItems[index]->file;
	BPopUpMenu* menu = new BPopUpMenu("context", false, false);

	BMessage* message = new BMessage(kMsgOpenItem);
	message->AddInt32("index", index);
	menu->AddItem(new BMenuItem(file.isFolder ? "Open folder" : "Open",
		message));

	if (!file.isFolder) {
		message = new BMessage(kMsgOpenItem);
		message->AddInt32("index", index);
		message->AddBool("newWindow", true);
		menu->AddItem(new BMenuItem("Open in new window", message));
	}

	menu->AddSeparatorItem();
	message = new BMessage(kMsgShowInTracker);
	message->AddInt32("index", index);
	menu->AddItem(new BMenuItem("Show in Tracker", message));

	menu->SetTargetForItems(Window());
	menu->SetAsyncAutoDestruct(true);
	menu->Go(ConvertToScreen(where), true, true, true);
}


void
ThumbnailView::_NotifySelection()
{
	if (Window() != NULL)
		Window()->PostMessage(kMsgItemSelected);
}
