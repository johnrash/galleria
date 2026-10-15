/*
 * Copyright 2026, Galleria authors.
 * Distributed under the terms of the MIT License.
 */


#include "ViewerWindow.h"

#include <memory>

#include <math.h>
#include <string.h>

#include <Application.h>
#include <Bitmap.h>
#include <LayoutBuilder.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <Path.h>
#include <Screen.h>
#include <ScrollBar.h>

#include "Galleria.h"
#include "ImageFile.h"
#include "ImageView.h"
#include "Settings.h"
#include "WorkerThread.h"


static const size_t kMaxCachedImages = 4;
static const size_t kMaxCacheBytes = 512 * 1024 * 1024;

#define EM_DASH "\xe2\x80\x94"


ViewerWindow::ViewerWindow(BRect frame, const BMessage* openMessage)
	:
	ZoomableWindow(frame, kApplicationName, B_DOCUMENT_WINDOW,
		B_AUTO_UPDATE_SIZE_LIMITS),
	fIndex(0),
	fDirection(1),
	fLoading(false),
	fScanGeneration(0),
	fFullScreen(false)
{
	fMenuBar = _CreateMenuBar();
	fImageView = new ImageView("image");
	fHorizontalScrollBar = new BScrollBar("hscroll", fImageView, 0, 0,
		B_HORIZONTAL);
	fVerticalScrollBar = new BScrollBar("vscroll", fImageView, 0, 0,
		B_VERTICAL);

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(fMenuBar)
		.AddGrid(0.0f, 0.0f)
			.Add(fImageView, 0, 0)
			.Add(fVerticalScrollBar, 1, 0)
			.Add(fHorizontalScrollBar, 0, 1)
		.End();

	fLoader = new ImageLoader(BMessenger(this));
	fImageView->MakeFocus(true);

	if (openMessage != NULL)
		SetImages(openMessage);
}


ViewerWindow::~ViewerWindow()
{
	delete fLoader;
}


void
ViewerWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgOpenViewer:
			SetImages(message);
			Activate();
			break;

		case B_SIMPLE_DATA:
		{
			// images dropped from Tracker
			BMessage open(kMsgOpenViewer);
			entry_ref ref;
			int32 count = 0;
			for (int32 i = 0; message->FindRef("refs", i, &ref) == B_OK; i++) {
				BEntry entry(&ref, true);
				if (entry.IsFile()) {
					open.AddRef("refs", &ref);
					count++;
				}
			}
			if (count > 0) {
				open.AddBool("scanFolder", count == 1);
				SetImages(&open);
			}
			break;
		}

		case kMsgImageLoaded:
			_ImageLoaded(message);
			break;

		case kMsgFolderScanned:
			_FolderScanned(message);
			break;

		case kMsgNextImage:
			if (fIndex + 1 < (int32)fRefs.size())
				_ShowImage(fIndex + 1);
			reply_if_waiting(message);
			break;
		case kMsgPreviousImage:
			if (fIndex > 0)
				_ShowImage(fIndex - 1);
			reply_if_waiting(message);
			break;
		case kMsgFirstImage:
			if (!fRefs.empty())
				_ShowImage(0);
			reply_if_waiting(message);
			break;
		case kMsgLastImage:
			if (!fRefs.empty())
				_ShowImage((int32)fRefs.size() - 1);
			reply_if_waiting(message);
			break;

		case kMsgZoomIn:
		case kMsgZoomOut:
		{
			BPoint where;
			bool hasWhere = message->FindPoint("where", &where) == B_OK;
			if (message->what == kMsgZoomIn)
				fImageView->ZoomIn(hasWhere ? &where : NULL);
			else
				fImageView->ZoomOut(hasWhere ? &where : NULL);
			_ZoomChanged();
			reply_if_waiting(message);
			break;
		}
		case kMsgZoomOriginal:
			fImageView->SetZoom(1.0f);
			_ZoomChanged();
			reply_if_waiting(message);
			break;
		case kMsgZoomFitWindow:
			fImageView->SetZoomMode(kZoomFitWindow);
			_ZoomChanged();
			reply_if_waiting(message);
			break;
		case kMsgZoomFitWidth:
			fImageView->SetZoomMode(kZoomFitWidth);
			_ZoomChanged();
			reply_if_waiting(message);
			break;
		case kMsgEnlargeSmall:
		{
			bool enlarge = !Settings::EnlargeSmallImages();
			Settings::SetEnlargeSmallImages(enlarge);
			fImageView->SetEnlargeSmall(enlarge);
			_UpdateMenus();
			_UpdateTitle();
			reply_if_waiting(message);
			break;
		}
		case kMsgScaleChanged:
			_UpdateMenus();
			_UpdateTitle();
			break;

		case kMsgFullScreen:
			if (message->GetBool("escape", false) && !fFullScreen)
				PostMessage(B_QUIT_REQUESTED);
			else
				_SetFullScreen(!fFullScreen);
			reply_if_waiting(message);
			break;

		case kMsgShowInBrowser:
			_ShowInBrowser();
			break;

		case kMsgScrollBy:
			fImageView->ScrollByDelta(message->GetFloat("x", 0.0f),
				message->GetFloat("y", 0.0f));
			reply_if_waiting(message);
			break;

		case kMsgGetState:
			_ReplyState(message);
			break;

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


bool
ViewerWindow::QuitRequested()
{
	BRect frame = fFullScreen ? fWindowedFrame : Frame();
	Settings::SetViewerFrame(UnzoomedFrame(frame), IsZoomed(frame));
	be_app->PostMessage(kMsgWindowClosed);
	return true;
}


void
ViewerWindow::SetImages(const BMessage* message)
{
	fRefs.clear();
	entry_ref ref;
	for (int32 i = 0; message->FindRef("refs", i, &ref) == B_OK; i++)
		fRefs.push_back(ref);

	fScanGeneration++;
	BMessenger browser;
	if (message->FindMessenger("browser", &browser) == B_OK)
		fBrowser = browser;
	else
		fBrowser = BMessenger();

	if (fRefs.empty()) {
		_UpdateTitle();
		return;
	}

	int32 index = message->GetInt32("index", 0);
	if (index < 0 || index >= (int32)fRefs.size())
		index = 0;
	fIndex = index;

	if (message->GetBool("scanFolder", false) && fRefs.size() == 1)
		_ScanFolder(fRefs[0]);

	_ShowImage(index);
}


// #pragma mark - private


BMenuBar*
ViewerWindow::_CreateMenuBar()
{
	BMenuBar* menuBar = new BMenuBar("menuBar");

	BMenu* menu = new BMenu("File");
	menu->AddItem(new BMenuItem("Show in browser",
		new BMessage(kMsgShowInBrowser), 'B'));
	menu->AddSeparatorItem();
	BMenuItem* item = new BMenuItem("About " kApplicationName B_UTF8_ELLIPSIS,
		new BMessage(B_ABOUT_REQUESTED));
	item->SetTarget(be_app);
	menu->AddItem(item);
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem("Close", new BMessage(B_QUIT_REQUESTED), 'W'));
	item = new BMenuItem("Quit", new BMessage(B_QUIT_REQUESTED), 'Q');
	item->SetTarget(be_app);
	menu->AddItem(item);
	menuBar->AddItem(menu);

	menu = new BMenu("Go");
	fPreviousItem = new BMenuItem("Previous image",
		new BMessage(kMsgPreviousImage), B_LEFT_ARROW);
	menu->AddItem(fPreviousItem);
	fNextItem = new BMenuItem("Next image", new BMessage(kMsgNextImage),
		B_RIGHT_ARROW);
	menu->AddItem(fNextItem);
	menu->AddSeparatorItem();
	fFirstItem = new BMenuItem("First image", new BMessage(kMsgFirstImage),
		B_HOME);
	menu->AddItem(fFirstItem);
	fLastItem = new BMenuItem("Last image", new BMessage(kMsgLastImage),
		B_END);
	menu->AddItem(fLastItem);
	menuBar->AddItem(menu);

	menu = new BMenu("View");
	menu->AddItem(new BMenuItem("Zoom in", new BMessage(kMsgZoomIn), '+'));
	menu->AddItem(new BMenuItem("Zoom out", new BMessage(kMsgZoomOut), '-'));
	menu->AddSeparatorItem();
	fOriginalItem = new BMenuItem("Original size",
		new BMessage(kMsgZoomOriginal), '1');
	menu->AddItem(fOriginalItem);
	fFitWindowItem = new BMenuItem("Fit to window",
		new BMessage(kMsgZoomFitWindow), '0');
	menu->AddItem(fFitWindowItem);
	fFitWidthItem = new BMenuItem("Fit width",
		new BMessage(kMsgZoomFitWidth));
	menu->AddItem(fFitWidthItem);
	menu->AddSeparatorItem();
	fEnlargeItem = new BMenuItem("Enlarge small images to fit",
		new BMessage(kMsgEnlargeSmall));
	menu->AddItem(fEnlargeItem);
	menu->AddSeparatorItem();
	fFullScreenItem = new BMenuItem("Full screen",
		new BMessage(kMsgFullScreen), 'F');
	menu->AddItem(fFullScreenItem);
	menuBar->AddItem(menu);

	// '+' needs Shift on many keyboards; accept '=' as well
	AddShortcut('=', B_COMMAND_KEY, new BMessage(kMsgZoomIn));

	return menuBar;
}


void
ViewerWindow::_ShowImage(int32 index)
{
	if (fRefs.empty())
		return;
	if (index < 0)
		index = 0;
	if (index >= (int32)fRefs.size())
		index = (int32)fRefs.size() - 1;

	fDirection = index < fIndex ? -1 : 1;
	fIndex = index;
	const entry_ref& ref = fRefs[fIndex];

	LoadedImagePtr cached = _FindCached(ref);
	if (cached)
		_Display(cached);
	else {
		// keep showing the previous image until the new one is ready
		fLoading = true;
		fLoader->Load(ref);
		_UpdateTitle();
		_UpdateMenus();
	}

	if (fBrowser.IsValid()) {
		BMessage select(kMsgSelectRef);
		select.AddRef("ref", &ref);
		fBrowser.SendMessage(&select);
	}
	if (fFullScreen)
		be_app->ObscureCursor();
}


void
ViewerWindow::_Display(const LoadedImagePtr& image)
{
	fLoading = false;
	fImageView->SetImage(image, Settings::ZoomMode(), Settings::ZoomFactor(),
		Settings::EnlargeSmallImages());

	if (image->Bitmap() == NULL) {
		BString text;
		text << "Could not open \"" << image->ref.name << "\": ";
		if (image->status == B_ERROR || image->status == B_NO_TRANSLATOR
			|| image->status == B_ILLEGAL_DATA || image->status == B_BAD_DATA) {
			text << "the file is damaged or its format is not supported.";
		} else
			text << strerror(image->status);
		fImageView->SetMessage(text.String());
	} else {
		fImageView->SetMessage("");
		_AddToCache(image);
	}

	_UpdateTitle();
	_UpdateMenus();
	_Prefetch();
}


void
ViewerWindow::_ImageLoaded(BMessage* message)
{
	LoadedImage* loaded = (LoadedImage*)message->GetPointer("image");
	if (loaded == NULL)
		return;

	LoadedImagePtr image(loaded);
	bool isCurrent = fIndex >= 0 && fIndex < (int32)fRefs.size()
		&& image->ref == fRefs[fIndex];
	if (isCurrent && fLoading)
		_Display(image);
	else if (image->Bitmap() != NULL)
		_AddToCache(image);
}


void
ViewerWindow::_UpdateTitle()
{
	if (fRefs.empty()) {
		SetTitle(kApplicationName);
		return;
	}

	BString title(fRefs[fIndex].name);
	const LoadedImagePtr& image = fImageView->Image();
	if (fLoading)
		title << " " EM_DASH " loading" B_UTF8_ELLIPSIS;
	else if (image && image->Bitmap() != NULL) {
		title << " " EM_DASH " " << image->width << " × " << image->height
			<< " " EM_DASH " " << (int32)roundf(fImageView->Scale() * 100)
			<< "%";
	}
	if (fRefs.size() > 1) {
		title << " " EM_DASH " " << fIndex + 1 << " / "
			<< (int32)fRefs.size();
	}
	SetTitle(title.String());
}


void
ViewerWindow::_UpdateMenus()
{
	int32 count = (int32)fRefs.size();
	fPreviousItem->SetEnabled(fIndex > 0);
	fFirstItem->SetEnabled(fIndex > 0);
	fNextItem->SetEnabled(fIndex + 1 < count);
	fLastItem->SetEnabled(fIndex + 1 < count);

	int32 mode = fImageView->ZoomMode();
	fFitWindowItem->SetMarked(mode == kZoomFitWindow);
	fFitWidthItem->SetMarked(mode == kZoomFitWidth);
	fOriginalItem->SetMarked(mode == kZoomFixed
		&& fabsf(fImageView->Scale() - 1.0f) < 0.001f);
	fEnlargeItem->SetMarked(Settings::EnlargeSmallImages());
	fFullScreenItem->SetMarked(fFullScreen);
}


void
ViewerWindow::_ZoomChanged()
{
	// the zoom sticks: the next image (and the next start) uses it too
	int32 mode = fImageView->ZoomMode();
	Settings::SetZoom(mode, mode == kZoomFixed ? fImageView->Scale() : 0.0f);
	_UpdateMenus();
	_UpdateTitle();
}


void
ViewerWindow::_SetFullScreen(bool fullScreen)
{
	if (fullScreen == fFullScreen)
		return;
	fFullScreen = fullScreen;

	if (fullScreen) {
		fWindowedFrame = Frame();
		BRect screenFrame = BScreen(this).Frame();
		fMenuBar->Hide();
		fHorizontalScrollBar->Hide();
		fVerticalScrollBar->Hide();
		SetLook(B_NO_BORDER_WINDOW_LOOK);
		SetFlags(Flags() | B_NOT_MOVABLE | B_NOT_RESIZABLE | B_NOT_ZOOMABLE);
		MoveTo(screenFrame.LeftTop());
		ResizeTo(screenFrame.Width(), screenFrame.Height());
		fImageView->SetBackground(make_color(0, 0, 0));
		Activate();
		be_app->ObscureCursor();
	} else {
		SetLook(B_DOCUMENT_WINDOW_LOOK);
		SetFlags(Flags() & ~(B_NOT_MOVABLE | B_NOT_RESIZABLE | B_NOT_ZOOMABLE));
		fMenuBar->Show();
		fHorizontalScrollBar->Show();
		fVerticalScrollBar->Show();
		MoveTo(fWindowedFrame.LeftTop());
		ResizeTo(fWindowedFrame.Width(), fWindowedFrame.Height());
		fImageView->SetBackground(make_color(48, 48, 48));
	}
	fImageView->MakeFocus(true);
	_UpdateMenus();
}


void
ViewerWindow::_ShowInBrowser()
{
	if (fRefs.empty())
		return;
	const entry_ref& ref = fRefs[fIndex];

	if (fBrowser.IsValid()) {
		BMessage select(kMsgSelectRef);
		select.AddRef("ref", &ref);
		fBrowser.SendMessage(&select);
		fBrowser.SendMessage(kMsgActivate);
		return;
	}

	BEntry entry(&ref);
	BEntry parent;
	entry_ref parentRef;
	if (entry.GetParent(&parent) != B_OK || parent.GetRef(&parentRef) != B_OK)
		return;

	BMessage open(kMsgOpenBrowser);
	open.AddRef("folder", &parentRef);
	open.AddRef("select", &ref);
	be_app->PostMessage(&open);
}


void
ViewerWindow::_AddToCache(const LoadedImagePtr& image)
{
	for (std::deque<LoadedImagePtr>::iterator it = fCache.begin();
			it != fCache.end(); it++) {
		if ((*it)->ref == image->ref) {
			fCache.erase(it);
			break;
		}
	}
	fCache.push_front(image);

	size_t bytes = 0;
	for (size_t i = 0; i < fCache.size(); i++)
		bytes += fCache[i]->Bytes();

	// least recently used first out, but always keep the newest
	while (fCache.size() > kMaxCachedImages
		|| (fCache.size() > 1 && bytes > kMaxCacheBytes)) {
		bytes -= fCache.back()->Bytes();
		fCache.pop_back();
	}
}


LoadedImagePtr
ViewerWindow::_FindCached(const entry_ref& ref) const
{
	for (size_t i = 0; i < fCache.size(); i++) {
		if (fCache[i]->ref == ref)
			return fCache[i];
	}
	return LoadedImagePtr();
}


void
ViewerWindow::_Prefetch()
{
	// decode the image the user will most likely want next
	int32 next = fIndex + fDirection;
	if (next >= 0 && next < (int32)fRefs.size() && !_FindCached(fRefs[next]))
		fLoader->Prefetch(fRefs[next]);
}


void
ViewerWindow::_ScanFolder(const entry_ref& ref)
{
	BEntry entry(&ref, true);
	BEntry parent;
	entry_ref resolved;
	entry_ref folder;
	if (entry.GetRef(&resolved) != B_OK || entry.GetParent(&parent) != B_OK
		|| parent.GetRef(&folder) != B_OK) {
		return;
	}
	fRefs[fIndex] = resolved;

	uint32 generation = fScanGeneration;
	int32 sortKey = Settings::SortKey();
	bool descending = Settings::SortDescending();
	BMessenger target(this);
	std::function<void()> scan = [folder, resolved, generation, sortKey,
			descending, target]() {
		FileList* list = new FileList;
		scan_folder(folder, false, *list);
		sort_files(*list, sortKey, descending);
		BMessage message(kMsgFolderScanned);
		message.AddUInt32("generation", generation);
		message.AddRef("current", &resolved);
		message.AddPointer("list", list);
		if (target.SendMessage(&message) != B_OK)
			delete list;
	};
	if (start_worker_thread(scan) != B_OK)
		scan();
}


void
ViewerWindow::_FolderScanned(BMessage* message)
{
	FileList* list = (FileList*)message->GetPointer("list");
	if (list == NULL)
		return;
	std::unique_ptr<FileList> deleter(list);

	entry_ref current;
	if (message->GetUInt32("generation", 0) != fScanGeneration
		|| message->FindRef("current", &current) != B_OK || fRefs.empty()
		|| !(fRefs[fIndex] == current)) {
		return;
	}

	std::vector<entry_ref> refs;
	int32 found = -1;
	for (size_t i = 0; i < list->size(); i++) {
		if ((*list)[i].ref == current)
			found = (int32)i;
		refs.push_back((*list)[i].ref);
	}
	if (found < 0)
		return;

	fRefs.swap(refs);
	fIndex = found;
	_UpdateTitle();
	_UpdateMenus();
	_Prefetch();
}


void
ViewerWindow::_ReplyState(BMessage* message)
{
	BMessage reply(B_REPLY);
	if (!fRefs.empty())
		reply.AddString("path", BPath(&fRefs[fIndex]).Path());
	reply.AddInt32("index", fIndex);
	reply.AddInt32("count", (int32)fRefs.size());
	reply.AddBool("loading", fLoading);
	reply.AddInt32("zoomMode", fImageView->ZoomMode());
	reply.AddFloat("scale", fImageView->Scale());
	reply.AddInt32("settingsZoomMode", Settings::ZoomMode());
	reply.AddFloat("settingsZoomFactor", Settings::ZoomFactor());
	BRect bounds = fImageView->Bounds();
	reply.AddFloat("scrollX", bounds.left);
	reply.AddFloat("scrollY", bounds.top);
	reply.AddFloat("viewWidth", bounds.Width() + 1);
	reply.AddFloat("viewHeight", bounds.Height() + 1);
	const LoadedImagePtr& image = fImageView->Image();
	reply.AddInt32("imageWidth", image ? image->width : 0);
	reply.AddInt32("imageHeight", image ? image->height : 0);
	reply.AddInt32("cached", (int32)fCache.size());
	reply.AddBool("fullScreen", fFullScreen);
	reply.AddString("title", Title());
	message->SendReply(&reply);
}
