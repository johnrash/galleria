/*
 * Copyright 2026, Galleria authors.
 * Distributed under the terms of the MIT License.
 */
#ifndef IMAGE_VIEW_H
#define IMAGE_VIEW_H


#include <String.h>
#include <View.h>

#include "ImageLoader.h"


// Shows one image, centered when it is smaller than the view. The mouse
// wheel and trackpad scroll it (Shift scrolls sideways, Ctrl zooms around
// the pointer); dragging with the mouse pans.
class ImageView : public BView {
public:
								ImageView(const char* name);
	virtual						~ImageView();

	virtual	void				AttachedToWindow();
	virtual	void				Draw(BRect updateRect);
	virtual	void				FrameResized(float width, float height);
	virtual	void				KeyDown(const char* bytes, int32 numBytes);
	virtual	void				MessageReceived(BMessage* message);
	virtual	void				MouseDown(BPoint where);
	virtual	void				MouseMoved(BPoint where, uint32 transit,
									const BMessage* dragMessage);
	virtual	void				MouseUp(BPoint where);

			// Shows the image with the given zoom, scrolled to the top
			// (and centered horizontally).
			void				SetImage(const LoadedImagePtr& image,
									int32 zoomMode, float zoomFactor,
									bool enlargeSmall);
			const LoadedImagePtr& Image() const { return fImage; }
			void				SetMessage(const char* message);

			void				SetZoomMode(int32 mode);
			void				SetZoom(float factor,
									const BPoint* anchor = NULL);
			void				ZoomIn(const BPoint* anchor = NULL);
			void				ZoomOut(const BPoint* anchor = NULL);
			void				SetEnlargeSmall(bool enlarge);

			int32				ZoomMode() const { return fZoomMode; }
			float				Scale() const { return fScale; }

			void				ScrollByDelta(float x, float y);
			void				SetBackground(rgb_color color);

private:
			float				_ComputeScale(BSize viewSize) const;
			BSize				_ViewSize() const;
			BSize				_ScaledSize(float scale) const;
			BPoint				_Origin(float scale, BSize viewSize) const;
			BRect				_ImageRect() const;
			void				_UpdateScrollBars();
			void				_ScrollToClamped(float x, float y);
			void				_ChangeZoom(int32 mode, float factor,
									const BPoint* anchor);
			void				_ShowContextMenu(BPoint where);

private:
			LoadedImagePtr		fImage;
			BString				fMessage;
			int32				fZoomMode;
			float				fZoomFactor;
			float				fScale;
			bool				fEnlargeSmall;
			BSize				fLastViewSize;
			rgb_color			fBackground;

			bool				fDragging;
			BPoint				fDragStart;
			BPoint				fScrollStart;
};


#endif	// IMAGE_VIEW_H
