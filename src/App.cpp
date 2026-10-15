/*
 * Copyright 2026, Galleria authors.
 * Distributed under the terms of the MIT License.
 *
 * Galleria - a simple image browser and viewer for Haiku.
 */


#include "App.h"

#include <algorithm>

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <AboutWindow.h>
#include <Entry.h>
#include <FindDirectory.h>
#include <Path.h>
#include <Screen.h>

#include "BrowserWindow.h"
#include "Galleria.h"
#include "ImageFile.h"
#include "Settings.h"
#include "ViewerWindow.h"


static const float kCascadeOffset = 24.0f;


App::App()
	:
	BApplication(kApplicationSignature),
	fWindowCount(0)
{
	Settings::Load();

	// load the translators once here, before worker threads need them
	is_supported_image_type("image/png");
}


void
App::ArgvReceived(int32 argc, char** argv)
{
	const char* cwd = NULL;
	BMessage* current = CurrentMessage();
	if (current != NULL)
		current->FindString("cwd", &cwd);

	BMessage refs(B_REFS_RECEIVED);
	for (int32 i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
			printf("usage: %s [folder | image ...]\n", kApplicationName);
			continue;
		}

		BPath path;
		if (argv[i][0] == '/' || cwd == NULL)
			path.SetTo(argv[i]);
		else
			path.SetTo(cwd, argv[i]);

		entry_ref ref;
		if (path.InitCheck() == B_OK
			&& get_ref_for_path(path.Path(), &ref) == B_OK
			&& BEntry(&ref).Exists()) {
			refs.AddRef("refs", &ref);
		} else
			fprintf(stderr, "%s: %s: not found\n", kApplicationName, argv[i]);
	}

	if (refs.HasRef("refs"))
		RefsReceived(&refs);
}


void
App::RefsReceived(BMessage* message)
{
	BMessage open(kMsgOpenViewer);
	int32 files = 0;

	entry_ref ref;
	for (int32 i = 0; message->FindRef("refs", i, &ref) == B_OK; i++) {
		BEntry entry(&ref, true);
		if (entry.IsDirectory())
			_OpenBrowser(ref);
		else if (entry.Exists()) {
			open.AddRef("refs", &ref);
			files++;
		}
	}

	if (files > 0) {
		// one image: browse its whole folder; several: just those
		open.AddInt32("index", 0);
		open.AddBool("scanFolder", files == 1);
		_OpenViewer(&open);
	}
}


void
App::ReadyToRun()
{
	if (fWindowCount > 0)
		return;

	entry_ref ref;
	BString lastFolder = Settings::LastFolder();
	if (lastFolder.IsEmpty()
		|| get_ref_for_path(lastFolder.String(), &ref) != B_OK
		|| !BEntry(&ref).IsDirectory()) {
		BPath home;
		if (find_directory(B_USER_DIRECTORY, &home) != B_OK)
			home.SetTo("/boot/home");
		get_ref_for_path(home.Path(), &ref);
	}
	_OpenBrowser(ref);
}


void
App::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgOpenViewer:
			_OpenViewer(message);
			break;

		case kMsgOpenBrowser:
		{
			entry_ref folder;
			entry_ref select;
			if (message->FindRef("folder", &folder) == B_OK) {
				_OpenBrowser(folder, message->FindRef("select", &select)
					== B_OK ? &select : NULL);
			}
			break;
		}

		case kMsgWindowClosed:
			Settings::Save();
			if (--fWindowCount <= 0)
				PostMessage(B_QUIT_REQUESTED);
			break;

		default:
			BApplication::MessageReceived(message);
			break;
	}
}


bool
App::QuitRequested()
{
	if (!BApplication::QuitRequested())
		return false;

	Settings::Save();
	return true;
}


void
App::AboutRequested()
{
	BAboutWindow* window = new BAboutWindow(kApplicationName,
		kApplicationSignature);
	window->AddDescription("A simple image browser and viewer. It shows "
		"every image format the installed translators can read.\n\n"
		"Thumbnails are cached in ~/config/cache/Galleria; the folder can be "
		"deleted at any time.");
	window->AddCopyright(2026, "Galleria authors");
	window->Show();
}


void
App::_OpenBrowser(const entry_ref& folder, const entry_ref* select)
{
	bool zoom;
	BRect frame = _WindowFrame(Settings::BrowserFrame(),
		Settings::BrowserZoomed(), 920, 660, &zoom);
	if (fWindowCount > 0)
		frame.OffsetBy(kCascadeOffset, kCascadeOffset);

	BrowserWindow* window = new BrowserWindow(frame, folder, select);
	if (zoom)
		window->ToggleZoom();
	window->Show();
	fWindowCount++;
}


void
App::_OpenViewer(BMessage* message)
{
	bool newWindow = message->GetBool("newWindow", false);
	if (!newWindow && fViewer.IsValid()) {
		// reuse the viewer window
		fViewer.SendMessage(message);
		return;
	}

	bool zoom;
	BRect frame = _WindowFrame(Settings::ViewerFrame(),
		Settings::ViewerZoomed(), 1000, 750, &zoom);
	if (fViewer.IsValid())
		frame.OffsetBy(kCascadeOffset, kCascadeOffset);

	ViewerWindow* window = new ViewerWindow(frame, message);
	if (zoom)
		window->ToggleZoom();
	window->Show();
	fWindowCount++;

	if (!newWindow || !fViewer.IsValid())
		fViewer = BMessenger(window);
}


// Returns the saved (normal) frame if it is still (mostly) on screen, a
// centered default frame otherwise. *_zoom tells whether to zoom the window
// after creating it.
BRect
App::_WindowFrame(BRect saved, bool savedZoomed, float width, float height,
	bool* _zoom) const
{
	BRect screen = BScreen().Frame();
	*_zoom = savedZoomed;

	if (saved.IsValid() && saved.Width() >= 200 && saved.Height() >= 150
		&& screen.InsetByCopy(20, 20).Intersects(saved)
		&& saved.top >= screen.top + 10) {
		return saved;
	}

	width = std::min(width, screen.Width() - 40);
	height = std::min(height, screen.Height() - 60);
	BRect frame(0, 0, width - 1, height - 1);
	frame.OffsetTo(screen.left + floorf((screen.Width() - width) / 2),
		screen.top + floorf((screen.Height() - height) / 2));
	return frame;
}


int
main()
{
	App app;
	app.Run();
	return 0;
}
