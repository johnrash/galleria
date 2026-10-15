/*
 * Copyright 2026, Galleria authors.
 * Distributed under the terms of the MIT License.
 */
#ifndef ZOOMABLE_WINDOW_H
#define ZOOMABLE_WINDOW_H


#include <Window.h>


// A window that remembers its normal frame while it is zoomed (maximized
// with the zoom button), so that it can be saved and restored as
// "normal frame + zoomed".
//
// BWindow::Zoom() only knows the frame to go back to within one session: a
// window that is created at its zoomed size (from saved settings) has
// nothing to return to, and the zoom button then only ever maximizes. Here a
// press on the zoom button always changes the window's size.
class ZoomableWindow : public BWindow {
public:
								ZoomableWindow(BRect frame, const char* title,
									window_type type, uint32 flags);

	virtual	void				DispatchMessage(BMessage* message,
									BHandler* handler);

			// Zooms or unzooms, like the zoom button.
			void				ToggleZoom();

			// For saving, with the window's frame (outside of full screen):
			// whether it is zoomed, and the frame to go back to.
			bool				IsZoomed(BRect frame) const;
			BRect				UnzoomedFrame(BRect frame) const;

private:
			BRect				fUnzoomedFrame;
			BRect				fZoomedFrame;
			bool				fZoomed;
};


#endif	// ZOOMABLE_WINDOW_H
