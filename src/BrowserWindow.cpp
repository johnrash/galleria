/*
 * Copyright 2026, Galleria authors.
 * Distributed under the terms of the MIT License.
 */


#include "BrowserWindow.h"

#include <stdio.h>
#include <string.h>

#include <Alert.h>
#include <Application.h>
#include <Bitmap.h>
#include <Beep.h>
#include <Button.h>
#include <DateTimeFormat.h>
#include <FilePanel.h>
#include <FindDirectory.h>
#include <Invoker.h>
#include <LayoutBuilder.h>
#include <MenuBar.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <MessageRunner.h>
#include <NodeMonitor.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <ScrollView.h>
#include <SeparatorView.h>
#include <Slider.h>
#include <StringView.h>
#include <TextControl.h>

#include "Galleria.h"
#include "ImageFile.h"
#include "Settings.h"
#include "ThumbnailLoader.h"
#include "ThumbnailView.h"
#include "WorkerThread.h"


static const char* const kSortLabels[kSortKeyCount] = {
	"Name",
	"Modification date",
	"Creation date",
	"Size",
	"Type"
};

static const bigtime_t kRescanDelay = 300000;

#define BULLET "\xe2\x80\xa2"


// An upwards arrow for the parent folder button, drawn in the control text
// color so that it follows the system colors.
static BBitmap*
create_up_icon(int32 size)
{
	BBitmap* bitmap = new BBitmap(BRect(0, 0, size - 1, size - 1),
		B_BITMAP_ACCEPTS_VIEWS, B_RGBA32);
	if (bitmap->InitCheck() != B_OK) {
		delete bitmap;
		return NULL;
	}
	memset(bitmap->Bits(), 0, bitmap->BitsLength());

	BView* view = new BView(bitmap->Bounds(), "icon", B_FOLLOW_NONE,
		B_WILL_DRAW);
	bitmap->AddChild(view);
	bitmap->Lock();

	float s = size;
	BPoint points[] = {
		BPoint(s / 2, 1), BPoint(s - 2, s / 2), BPoint(s * 0.66f, s / 2),
		BPoint(s * 0.66f, s - 2), BPoint(s * 0.34f, s - 2),
		BPoint(s * 0.34f, s / 2), BPoint(1, s / 2)
	};
	view->SetDrawingMode(B_OP_ALPHA);
	view->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_COMPOSITE);
	view->SetHighColor(ui_color(B_CONTROL_TEXT_COLOR));
	view->FillPolygon(points, sizeof(points) / sizeof(points[0]));
	view->Sync();

	bitmap->Unlock();
	return bitmap;
}


// #pragma mark -


BrowserWindow::BrowserWindow(BRect frame, const entry_ref& folder,
	const entry_ref* select)
	:
	ZoomableWindow(frame, kApplicationName, B_DOCUMENT_WINDOW,
		B_AUTO_UPDATE_SIZE_LIMITS),
	fScanGeneration(0),
	fScanning(false),
	fScanSameFolder(false),
	fHasPendingSelect(false),
	fRescanRunner(NULL),
	fFolderPanel(NULL)
{
	BMenuBar* menuBar = _CreateMenuBar();

	fUpButton = new BButton("up", NULL, new BMessage(kMsgParentFolder));
	float iconSize = roundf(be_plain_font->Size() * 1.2f);
	BBitmap* icon = create_up_icon((int32)iconSize);
	if (icon != NULL) {
		fUpButton->SetIcon(icon);
		delete icon;
	} else
		fUpButton->SetLabel("Up");
	fUpButton->SetToolTip("Parent folder");

	fLocation = new BTextControl("location", NULL, "",
		new BMessage(kMsgLocationEntered));

	BPopUpMenu* sortPopUp = new BPopUpMenu("sort");
	_AddSortItems(sortPopUp);
	fSortField = new BMenuField("sortBy", "Sort by:", sortPopUp);

	BPopUpMenu* orderPopUp = new BPopUpMenu("order");
	_AddOrderItems(orderPopUp);
	fOrderField = new BMenuField("order", NULL, orderPopUp);

	fView = new ThumbnailView("thumbnails");
	BScrollView* scrollView = new BScrollView("scrollView", fView, 0, false,
		true, B_NO_BORDER);

	fStatus = new BStringView("status", "");
	fStatus->SetTruncation(B_TRUNCATE_END);
	fStatus->SetExplicitMinSize(BSize(60, B_SIZE_UNSET));
	fStatus->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

	BStringView* sizeLabel = new BStringView("sizeLabel", "Thumbnail size:");

	fSizeSlider = new BSlider("thumbSize", NULL, new BMessage(kMsgThumbSize),
		kMinThumbnailSize, kMaxThumbnailSize, B_HORIZONTAL);
	fSizeSlider->SetModificationMessage(new BMessage(kMsgThumbSize));
	fSizeSlider->SetHashMarks(B_HASH_MARKS_NONE);
	fSizeSlider->SetKeyIncrementValue(16);
	fSizeSlider->SetValue(fView->ThumbnailSize());
	fSizeSlider->SetExplicitMinSize(BSize(120, B_SIZE_UNSET));
	fSizeSlider->SetExplicitMaxSize(BSize(220, B_SIZE_UNSET));
	fSizeSlider->SetToolTip("Thumbnail size (Ctrl + mouse wheel)");

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(menuBar)
		.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
			.SetInsets(B_USE_SMALL_SPACING, B_USE_SMALL_SPACING,
				B_USE_SMALL_SPACING, B_USE_SMALL_SPACING)
			.Add(fUpButton)
			.Add(fLocation, 10)
			.Add(fSortField, 0)
			.Add(fOrderField, 0)
		.End()
		.Add(new BSeparatorView(B_HORIZONTAL))
		.Add(scrollView, 100)
		.Add(new BSeparatorView(B_HORIZONTAL))
		.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
			.SetInsets(B_USE_SMALL_SPACING, 2, B_USE_ITEM_SPACING, 2)
			.Add(fStatus, 10)
			.Add(sizeLabel, 0)
			.Add(fSizeSlider, 0)
		.End();

	_UpdateSortMarks();

	SetSizeLimits(420, B_SIZE_UNLIMITED, 300, B_SIZE_UNLIMITED);
	fView->MakeFocus(true);

	BEntry entry(&folder, true);
	if (entry.IsDirectory()) {
		node_ref selectNode;
		bool hasSelect = false;
		if (select != NULL) {
			BEntry selectEntry(select, true);
			hasSelect = selectEntry.GetNodeRef(&selectNode) == B_OK;
		}
		SetFolder(folder, hasSelect ? &selectNode : NULL);
	}
}


BrowserWindow::~BrowserWindow()
{
	stop_watching(this);
	delete fRescanRunner;
	delete fFolderPanel;
}


void
BrowserWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgScanDone:
			_ScanDone(message);
			break;

		case B_NODE_MONITOR:
			// coalesce bursts of changes (a copy of many files) into one scan
			if (fRescanRunner == NULL) {
				BMessage rescan(kMsgRescan);
				fRescanRunner = new BMessageRunner(BMessenger(this), &rescan,
					kRescanDelay, 1);
			}
			break;

		case kMsgRescan:
			delete fRescanRunner;
			fRescanRunner = NULL;
			_StartScan(true);
			break;

		case kMsgReload:
			_StartScan(true);
			reply_if_waiting(message);
			break;

		case kMsgOpenFolderPanel:
			if (fFolderPanel == NULL) {
				BMessenger target(this);
				fFolderPanel = new BFilePanel(B_OPEN_PANEL, &target, &fFolder,
					B_DIRECTORY_NODE, false, new BMessage(kMsgFolderChosen));
				fFolderPanel->Window()->SetTitle(
					kApplicationName ": Open folder");
				fFolderPanel->SetButtonLabel(B_DEFAULT_BUTTON, "Open");
			} else
				fFolderPanel->SetPanelDirectory(&fFolder);
			fFolderPanel->Show();
			break;

		case kMsgFolderChosen:
		case B_SIMPLE_DATA:
		{
			entry_ref ref;
			if (message->FindRef("refs", &ref) == B_OK)
				_GoToRef(ref);
			break;
		}

		case kMsgGoToFolder:
		{
			const char* path;
			if (message->FindString("path", &path) == B_OK)
				_GoToPath(path);
			reply_if_waiting(message);
			break;
		}

		case kMsgLocationEntered:
			_GoToPath(fLocation->Text());
			break;

		case kMsgParentFolder:
			_GoToParent();
			reply_if_waiting(message);
			break;

		case kMsgSortBy:
		{
			int32 key;
			if (message->FindInt32("key", &key) == B_OK && key >= 0
				&& key < kSortKeyCount) {
				_ApplySorting(key, Settings::SortDescending());
			}
			reply_if_waiting(message);
			break;
		}

		case kMsgSortOrder:
		{
			bool descending;
			if (message->FindBool("descending", &descending) == B_OK)
				_ApplySorting(Settings::SortKey(), descending);
			reply_if_waiting(message);
			break;
		}

		case kMsgShowFolders:
		{
			bool show = !Settings::ShowFolders();
			Settings::SetShowFolders(show);
			fShowFoldersItem->SetMarked(show);
			_StartScan(true);
			reply_if_waiting(message);
			break;
		}

		case kMsgThumbSize:
			_SetThumbnailSize(fSizeSlider->Value());
			break;

		case kMsgThumbSizeStep:
		{
			int32 delta = message->GetInt32("delta", 0);
			int32 size = fView->ThumbnailSize();
			// step to the next multiple of the step size
			if (delta > 0)
				size = (size / kThumbnailSizeStep + 1) * kThumbnailSizeStep;
			else if (delta < 0) {
				size = ((size + kThumbnailSizeStep - 1) / kThumbnailSizeStep
					- 1) * kThumbnailSizeStep;
			}
			_SetThumbnailSize(size);
			reply_if_waiting(message);
			break;
		}

		case kMsgSetThumbSize:
		{
			int32 size;
			if (message->FindInt32("size", &size) == B_OK)
				_SetThumbnailSize(size);
			reply_if_waiting(message);
			break;
		}

		case kMsgItemSelected:
			_UpdateStatus();
			break;

		case kMsgSelectIndex:
		{
			int32 index = message->GetInt32("index", -1);
			fView->Select(index);
			fView->ScrollToItem(index);
			reply_if_waiting(message);
			break;
		}

		case kMsgSelectRef:
		{
			entry_ref ref;
			if (message->FindRef("ref", &ref) == B_OK)
				fView->SelectRef(ref);
			break;
		}

		case kMsgActivate:
			Activate();
			break;

		case kMsgOpenItem:
			_OpenItem(message->GetInt32("index", -1),
				message->GetBool("newWindow", false));
			reply_if_waiting(message);
			break;

		case kMsgShowInTracker:
			_ShowInTracker(message->GetInt32("index", -1));
			break;

		case kMsgClearCache:
			_ClearCache();
			break;

		case kMsgClearCacheConfirmed:
			if (message->GetInt32("which", 0) == 1) {
				ThumbnailLoader::ClearCache();
				fStatus->SetText("Thumbnail cache cleared.");
			}
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
BrowserWindow::QuitRequested()
{
	Settings::SetBrowserFrame(UnzoomedFrame(Frame()), IsZoomed(Frame()));
	be_app->PostMessage(kMsgWindowClosed);
	return true;
}


void
BrowserWindow::SetFolder(const entry_ref& folder, const node_ref* select)
{
	BEntry entry(&folder, true);
	entry_ref ref;
	if (!entry.IsDirectory() || entry.GetRef(&ref) != B_OK) {
		beep();
		return;
	}

	fFolder = ref;
	entry.GetNodeRef(&fFolderNode);

	stop_watching(this);
	watch_node(&fFolderNode, B_WATCH_DIRECTORY, this);

	BPath path(&entry);
	Settings::SetLastFolder(path.Path());
	fLocation->SetText(path.Path());

	BString title(kApplicationName);
	title << ": " << (path.Leaf() != NULL && path.Leaf()[0] != '\0'
		? path.Leaf() : path.Path());
	SetTitle(title.String());

	BEntry parent;
	bool hasParent = entry.GetParent(&parent) == B_OK;
	fUpButton->SetEnabled(hasParent);
	fParentItem->SetEnabled(hasParent);

	fHasPendingSelect = select != NULL;
	if (select != NULL)
		fPendingSelect = *select;

	_StartScan(false);
}


// #pragma mark - private


BMenuBar*
BrowserWindow::_CreateMenuBar()
{
	BMenuBar* menuBar = new BMenuBar("menuBar");

	BMenu* menu = new BMenu("File");
	menu->AddItem(new BMenuItem("Open folder" B_UTF8_ELLIPSIS,
		new BMessage(kMsgOpenFolderPanel), 'O'));
	fParentItem = new BMenuItem("Parent folder",
		new BMessage(kMsgParentFolder), B_UP_ARROW);
	menu->AddItem(fParentItem);
	menu->AddItem(new BMenuItem("Reload", new BMessage(kMsgReload), 'R'));
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem("Clear thumbnail cache" B_UTF8_ELLIPSIS,
		new BMessage(kMsgClearCache)));
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

	menu = new BMenu("View");
	fSortMenu = new BMenu("Sort by");
	fSortMenu->SetRadioMode(true);
	_AddSortItems(fSortMenu);
	menu->AddItem(fSortMenu);
	fOrderMenu = new BMenu("Order");
	fOrderMenu->SetRadioMode(true);
	_AddOrderItems(fOrderMenu);
	menu->AddItem(fOrderMenu);
	menu->AddSeparatorItem();
	fShowFoldersItem = new BMenuItem("Show folders",
		new BMessage(kMsgShowFolders));
	fShowFoldersItem->SetMarked(Settings::ShowFolders());
	menu->AddItem(fShowFoldersItem);
	menu->AddSeparatorItem();

	BMessage* larger = new BMessage(kMsgThumbSizeStep);
	larger->AddInt32("delta", 1);
	menu->AddItem(new BMenuItem("Larger thumbnails", larger, '+'));
	BMessage* smaller = new BMessage(kMsgThumbSizeStep);
	smaller->AddInt32("delta", -1);
	menu->AddItem(new BMenuItem("Smaller thumbnails", smaller, '-'));
	menuBar->AddItem(menu);

	// '+' needs Shift on many keyboards; accept '=' as well
	BMessage* alsoLarger = new BMessage(kMsgThumbSizeStep);
	alsoLarger->AddInt32("delta", 1);
	AddShortcut('=', B_COMMAND_KEY, alsoLarger);

	return menuBar;
}


void
BrowserWindow::_AddSortItems(BMenu* menu)
{
	for (int32 key = 0; key < kSortKeyCount; key++) {
		BMessage* message = new BMessage(kMsgSortBy);
		message->AddInt32("key", key);
		menu->AddItem(new BMenuItem(kSortLabels[key], message));
	}
}


void
BrowserWindow::_AddOrderItems(BMenu* menu)
{
	BMessage* message = new BMessage(kMsgSortOrder);
	message->AddBool("descending", false);
	menu->AddItem(new BMenuItem("Ascending", message));
	message = new BMessage(kMsgSortOrder);
	message->AddBool("descending", true);
	menu->AddItem(new BMenuItem("Descending", message));
}


void
BrowserWindow::_UpdateSortMarks()
{
	int32 key = Settings::SortKey();
	int32 order = Settings::SortDescending() ? 1 : 0;

	BMenu* sortMenus[] = { fSortMenu, fSortField->Menu() };
	BMenu* orderMenus[] = { fOrderMenu, fOrderField->Menu() };
	for (int32 i = 0; i < 2; i++) {
		BMenuItem* item = sortMenus[i]->ItemAt(key);
		if (item != NULL)
			item->SetMarked(true);
		item = orderMenus[i]->ItemAt(order);
		if (item != NULL)
			item->SetMarked(true);
	}
}


void
BrowserWindow::_ApplySorting(int32 key, bool descending)
{
	Settings::SetSorting(key, descending);
	_UpdateSortMarks();
	fView->SetSorting(key, descending);
	_UpdateStatus();
}


void
BrowserWindow::_SetThumbnailSize(int32 size)
{
	if (size < kMinThumbnailSize)
		size = kMinThumbnailSize;
	if (size > kMaxThumbnailSize)
		size = kMaxThumbnailSize;

	if (fSizeSlider->Value() != size)
		fSizeSlider->SetValue(size);
	fView->SetThumbnailSize(size);
	Settings::SetThumbnailSize(size);
}


void
BrowserWindow::_StartScan(bool sameFolder)
{
	fScanGeneration++;
	if (!fScanning)
		fScanSameFolder = sameFolder;
	else
		fScanSameFolder = fScanSameFolder && sameFolder;
	fScanning = true;

	if (!sameFolder) {
		// show the new folder at once, even if reading it takes a while
		FileList empty;
		fView->SetFiles(empty, false);
		fView->SetEmptyText("Reading folder" B_UTF8_ELLIPSIS);
		_UpdateStatus();
	}

	// read the folder on its own thread: network or slow disks must not
	// freeze the window
	entry_ref folder = fFolder;
	bool includeFolders = Settings::ShowFolders();
	uint32 generation = fScanGeneration;
	BMessenger target(this);
	std::function<void()> scan = [folder, includeFolders, generation,
			target]() {
		FileList* list = new FileList;
		status_t status = scan_folder(folder, includeFolders, *list);
		BMessage message(kMsgScanDone);
		message.AddUInt32("generation", generation);
		message.AddInt32("status", status);
		message.AddPointer("list", list);
		if (target.SendMessage(&message) != B_OK)
			delete list;
	};
	if (start_worker_thread(scan) != B_OK)
		scan();
}


void
BrowserWindow::_ScanDone(BMessage* message)
{
	FileList* list = (FileList*)message->GetPointer("list");
	if (list == NULL)
		return;
	if (message->GetUInt32("generation", 0) != fScanGeneration) {
		// an older scan; a newer one is on its way
		delete list;
		return;
	}

	fScanning = false;
	status_t status = message->GetInt32("status", B_ERROR);
	if (status != B_OK) {
		BString text("Could not read this folder: ");
		text << strerror(status);
		fView->SetEmptyText(text.String());
	} else
		fView->SetEmptyText("No images in this folder");

	fView->SetFiles(*list, fScanSameFolder,
		fHasPendingSelect ? &fPendingSelect : NULL);
	fHasPendingSelect = false;
	delete list;

	_UpdateStatus();
}


void
BrowserWindow::_UpdateStatus()
{
	if (fScanning && fView->CountItems() == 0) {
		fStatus->SetText("Reading folder" B_UTF8_ELLIPSIS);
		return;
	}

	BString text;
	int32 selection = fView->CurrentSelection();
	const FileItem* item = fView->ItemAt(selection);
	if (item != NULL) {
		text = item->name;
		if (item->isFolder)
			text << "  " BULLET "  folder";
		else {
			int32 width, height;
			if (fView->GetImageSize(selection, &width, &height)) {
				text << "  " BULLET "  " << width << " × " << height;
			}
			text << "  " BULLET "  " << format_size(item->size);
		}

		BDateTimeFormat format;
		BString date;
		if (format.Format(date, (time_t)(item->modified / 1000000000LL),
				B_SHORT_DATE_FORMAT, B_SHORT_TIME_FORMAT) == B_OK) {
			text << "  " BULLET "  " << date;
		}
	} else {
		int32 images = fView->CountImages();
		int32 folders = fView->CountItems() - images;
		text << images << (images == 1 ? " image" : " images");
		if (folders > 0)
			text << ", " << folders << (folders == 1 ? " folder" : " folders");
	}
	fStatus->SetText(text.String());
}


void
BrowserWindow::_GoToParent()
{
	BEntry entry(&fFolder);
	BEntry parent;
	entry_ref parentRef;
	if (entry.GetParent(&parent) != B_OK || parent.GetRef(&parentRef) != B_OK)
		return;

	// select the folder we came from
	node_ref current = fFolderNode;
	SetFolder(parentRef, &current);
}


void
BrowserWindow::_GoToPath(const char* path)
{
	BString expanded(path);
	expanded.Trim();
	if (expanded == "~" || expanded.StartsWith("~/")) {
		BPath home;
		if (find_directory(B_USER_DIRECTORY, &home) == B_OK)
			expanded.Replace("~", home.Path(), 1);
	}

	entry_ref ref;
	if (get_ref_for_path(expanded.String(), &ref) != B_OK
		|| !BEntry(&ref).Exists()) {
		beep();
		fLocation->SetText(BPath(&fFolder).Path());
		return;
	}
	_GoToRef(ref);
}


// Opens a folder, or the folder of a file with the file selected.
void
BrowserWindow::_GoToRef(const entry_ref& ref)
{
	BEntry entry(&ref, true);
	if (entry.IsDirectory()) {
		entry_ref folder;
		if (entry.GetRef(&folder) == B_OK)
			SetFolder(folder);
		return;
	}

	BEntry parent;
	entry_ref parentRef;
	node_ref node;
	if (entry.GetParent(&parent) != B_OK || parent.GetRef(&parentRef) != B_OK
		|| entry.GetNodeRef(&node) != B_OK) {
		return;
	}

	// a file in this folder: just select it
	entry_ref fileRef;
	if (parentRef == fFolder && !fScanning && entry.GetRef(&fileRef) == B_OK
		&& fView->SelectRef(fileRef)) {
		fLocation->SetText(BPath(&fFolder).Path());
		return;
	}
	SetFolder(parentRef, &node);
}


void
BrowserWindow::_OpenItem(int32 index, bool newWindow)
{
	const FileItem* item = fView->ItemAt(index);
	if (item == NULL)
		return;

	if (item->isFolder) {
		entry_ref ref = item->ref;
		SetFolder(ref);
		return;
	}

	BMessage open(kMsgOpenViewer);
	int32 imageIndex;
	fView->AddImageRefs(open, index, &imageIndex);
	open.AddInt32("index", imageIndex);
	open.AddBool("newWindow", newWindow);
	open.AddMessenger("browser", BMessenger(this));
	be_app->PostMessage(&open);
}


void
BrowserWindow::_ShowInTracker(int32 index)
{
	const FileItem* item = fView->ItemAt(index);
	if (item == NULL)
		return;

	BEntry entry(&item->ref);
	BEntry parent;
	entry_ref parentRef;
	node_ref node;
	if (entry.GetParent(&parent) != B_OK || parent.GetRef(&parentRef) != B_OK
		|| entry.GetNodeRef(&node) != B_OK) {
		return;
	}

	// Tracker opens the folder and selects the file
	BMessage message(B_REFS_RECEIVED);
	message.AddRef("refs", &parentRef);
	message.AddData("nodeRefToSelect", B_RAW_TYPE, &node, sizeof(node_ref));
	BMessenger("application/x-vnd.Be-TRAK").SendMessage(&message);
}


void
BrowserWindow::_ClearCache()
{
	int32 files;
	off_t bytes;
	ThumbnailLoader::GetCacheUsage(&files, &bytes);

	BString text;
	text << "Delete all cached thumbnails?\n\n"
		<< ThumbnailLoader::CacheDirectory().Path() << "\n"
		<< files << (files == 1 ? " file, " : " files, ") << format_size(bytes)
		<< "\n\nThumbnails are made again when they are needed.";

	BAlert* alert = new BAlert("Clear thumbnail cache", text.String(),
		"Cancel", "Delete", NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
	alert->SetShortcut(0, B_ESCAPE);
	alert->Go(new BInvoker(new BMessage(kMsgClearCacheConfirmed), this));
}


void
BrowserWindow::_ReplyState(BMessage* message)
{
	BMessage reply(B_REPLY);
	reply.AddString("path", BPath(&fFolder).Path());
	reply.AddInt32("items", fView->CountItems());
	reply.AddInt32("images", fView->CountImages());
	reply.AddInt32("selection", fView->CurrentSelection());
	reply.AddInt32("sortKey", Settings::SortKey());
	reply.AddBool("descending", Settings::SortDescending());
	reply.AddInt32("thumbnailSize", fView->ThumbnailSize());
	reply.AddInt32("slider", fSizeSlider->Value());
	reply.AddString("sortLabel", fSortField->Menu()->FindMarked() != NULL
		? fSortField->Menu()->FindMarked()->Label() : "");
	reply.AddString("orderLabel", fOrderField->Menu()->FindMarked() != NULL
		? fOrderField->Menu()->FindMarked()->Label() : "");
	reply.AddBool("scanning", fScanning);
	reply.AddInt32("loaded", fView->CountLoadedThumbnails());
	reply.AddInt32("pending", fView->CountPendingThumbnails());
	int32 visible, done;
	fView->CountVisibleThumbnails(&visible, &done);
	reply.AddInt32("visible", visible);
	reply.AddInt32("visibleDone", done);
	reply.AddString("status", fStatus->Text());
	reply.AddString("title", Title());
	for (int32 i = 0; i < fView->CountItems(); i++)
		reply.AddString("names", fView->ItemAt(i)->name);
	message->SendReply(&reply);
}
