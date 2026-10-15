/*
 * Copyright 2026, Galleria authors.
 * Distributed under the terms of the MIT License.
 */


#include "ZoomableWindow.h"

#include <math.h>

#include "Galleria.h"


ZoomableWindow::ZoomableWindow(BRect frame, const char* title,
	window_type type, uint32 flags)
	:
	BWindow(frame, title, type, flags),
	fZoomed(false)
{
}


void
ZoomableWindow::DispatchMessage(BMessage* message, BHandler* handler)
{
	if (message->what == B_ZOOM) {
		// the zoom button (BWindow handles B_ZOOM here, not in
		// MessageReceived())
		if ((Flags() & B_NOT_ZOOMABLE) == 0)
			ToggleZoom();
		reply_if_waiting(message);
		return;
	}

	BWindow::DispatchMessage(message, handler);
}


void
ZoomableWindow::ToggleZoom()
{
	BRect frame = Frame();
	if (IsZoomed(frame)) {
		MoveTo(fUnzoomedFrame.LeftTop());
		ResizeTo(fUnzoomedFrame.Width(), fUnzoomedFrame.Height());
		fZoomed = false;
		return;
	}

	// BWindow::Zoom() knows the area to fill (the screen without the
	// Deskbar, within the size limits)
	fUnzoomedFrame = frame;
	BWindow::Zoom();
	fZoomedFrame = Frame();
	fZoomed = true;

	if (fZoomedFrame.Width() == frame.Width()
		&& fZoomedFrame.Height() == frame.Height()) {
		// The window already had the zoomed size (saved that way by an
		// older Galleria, or enlarged by hand), so there is no smaller frame
		// to go back to: make one up, and go there.
		fUnzoomedFrame = fZoomedFrame.InsetByCopy(
			roundf(fZoomedFrame.Width() / 8),
			roundf(fZoomedFrame.Height() / 8));
		MoveTo(fUnzoomedFrame.LeftTop());
		ResizeTo(fUnzoomedFrame.Width(), fUnzoomedFrame.Height());
		fZoomed = false;
	}
}


bool
ZoomableWindow::IsZoomed(BRect frame) const
{
	// resizing a zoomed window by hand makes it a normal window again
	return fZoomed && frame.Width() == fZoomedFrame.Width()
		&& frame.Height() == fZoomedFrame.Height();
}


BRect
ZoomableWindow::UnzoomedFrame(BRect frame) const
{
	return IsZoomed(frame) ? fUnzoomedFrame : frame;
}
