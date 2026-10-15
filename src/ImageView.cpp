/*
 * Copyright 2026, Galleria authors.
 * Distributed under the terms of the MIT License.
 */


#include "ImageView.h"

#include <algorithm>

#include <math.h>

#include <Bitmap.h>
#include <Cursor.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <Region.h>
#include <ScrollBar.h>
#include <Window.h>

#include "Galleria.h"
#include "Settings.h"


static const float kZoomLevels[] = {
	0.02f, 0.05f, 0.1f, 0.15f, 0.2f, 0.25f, 1.0f / 3, 0.5f, 2.0f / 3, 0.75f,
	1.0f, 1.25f, 1.5f, 2.0f, 3.0f, 4.0f, 6.0f, 8.0f, 12.0f, 16.0f, 24.0f,
	32.0f
};
static const int32 kZoomLevelCount = sizeof(kZoomLevels) / sizeof(float);
static const float kMinZoom = 0.01f;
static const float kMaxZoom = 32.0f;

static const float kWheelStep = 64.0f;
	// pixels per wheel notch
static const float kKeyStep = 48.0f;

static const rgb_color kDefaultBackground = { 48, 48, 48, 255 };
static const rgb_color kMessageColor = { 210, 210, 210, 255 };


ImageView::ImageView(const char* name)
	:
	BView(name, B_WILL_DRAW | B_FRAME_EVENTS | B_NAVIGABLE),
	fZoomMode(kZoomFitWindow),
	fZoomFactor(1.0f),
	fScale(1.0f),
	fEnlargeSmall(false),
	fLastViewSize(0, 0),
	fBackground(kDefaultBackground),
	fDragging(false)
{
	SetViewColor(B_TRANSPARENT_COLOR);
	SetExplicitMinSize(BSize(100, 80));
}


ImageView::~ImageView()
{
}


void
ImageView::AttachedToWindow()
{
	BView::AttachedToWindow();
	fLastViewSize = _ViewSize();
	_UpdateScrollBars();
}


void
ImageView::Draw(BRect updateRect)
{
	SetLowColor(fBackground);

	BBitmap* bitmap = NULL;
	float levelScale = 1.0f;
	if (fImage)
		bitmap = fImage->LevelFor(fScale, &levelScale);

	if (bitmap == NULL) {
		FillRect(updateRect, B_SOLID_LOW);
		if (!fMessage.IsEmpty()) {
			BRect bounds = Bounds();
			font_height fontHeight;
			GetFontHeight(&fontHeight);
			float width = StringWidth(fMessage.String());
			SetHighColor(kMessageColor);
			SetDrawingMode(B_OP_OVER);
			DrawString(fMessage.String(),
				BPoint(floorf(bounds.left + (bounds.Width() - width) / 2),
					floorf(bounds.top + bounds.Height() / 2
						+ fontHeight.ascent / 2)));
			SetDrawingMode(B_OP_COPY);
		}
		return;
	}

	BRect imageRect = _ImageRect();
	bool opaque = bitmap->ColorSpace() != B_RGBA32;

	// paint only around opaque images: no flicker while scrolling
	BRegion background(updateRect);
	if (opaque)
		background.Exclude(imageRect);
	FillRegion(&background, B_SOLID_LOW);

	if (!imageRect.Intersects(updateRect))
		return;

	if (!opaque) {
		SetDrawingMode(B_OP_ALPHA);
		SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
	}
	// smooth up to 300%, show the real pixels beyond
	uint32 options = levelScale < 3.0f ? B_FILTER_BITMAP_BILINEAR : 0;
	DrawBitmap(bitmap, bitmap->Bounds(), imageRect, options);
	SetDrawingMode(B_OP_COPY);
}


void
ImageView::FrameResized(float width, float height)
{
	BSize oldViewSize = fLastViewSize;
	BSize viewSize = _ViewSize();
	float oldScale = fScale;
	fLastViewSize = viewSize;

	if (!fImage || fImage->Bitmap() == NULL || oldViewSize.width <= 0) {
		fScale = _ComputeScale(viewSize);
		_UpdateScrollBars();
		Invalidate();
		return;
	}

	// keep the image point in the middle of the view in the middle
	BPoint oldOrigin = _Origin(oldScale, oldViewSize);
	BPoint leftTop = Bounds().LeftTop();
	float imageX = (leftTop.x + oldViewSize.width / 2 - oldOrigin.x)
		/ oldScale;
	float imageY = (leftTop.y + oldViewSize.height / 2 - oldOrigin.y)
		/ oldScale;

	fScale = _ComputeScale(viewSize);
	_UpdateScrollBars();
	BPoint origin = _Origin(fScale, viewSize);
	_ScrollToClamped(imageX * fScale + origin.x - viewSize.width / 2,
		imageY * fScale + origin.y - viewSize.height / 2);
	Invalidate();

	if (fScale != oldScale && Window() != NULL)
		Window()->PostMessage(kMsgScaleChanged);
}


void
ImageView::KeyDown(const char* bytes, int32 numBytes)
{
	BSize viewSize = _ViewSize();
	uint32 command = 0;

	switch (bytes[0]) {
		case B_LEFT_ARROW:
		case B_BACKSPACE:
			command = kMsgPreviousImage;
			break;
		case B_RIGHT_ARROW:
		case B_SPACE:
			command = kMsgNextImage;
			break;
		case B_HOME:
			command = kMsgFirstImage;
			break;
		case B_END:
			command = kMsgLastImage;
			break;
		case B_UP_ARROW:
			ScrollByDelta(0, -kKeyStep);
			break;
		case B_DOWN_ARROW:
			ScrollByDelta(0, kKeyStep);
			break;
		case B_PAGE_UP:
			ScrollByDelta(0, -floorf(viewSize.height * 0.9f));
			break;
		case B_PAGE_DOWN:
			ScrollByDelta(0, floorf(viewSize.height * 0.9f));
			break;
		case '+':
		case '=':
			command = kMsgZoomIn;
			break;
		case '-':
			command = kMsgZoomOut;
			break;
		case '1':
			command = kMsgZoomOriginal;
			break;
		case 'f':
		case 'F':
			command = kMsgZoomFitWindow;
			break;
		case 'w':
		case 'W':
			command = kMsgZoomFitWidth;
			break;
		case B_ENTER:
			command = kMsgFullScreen;
			break;
		case B_ESCAPE:
		{
			BMessage message(kMsgFullScreen);
			message.AddBool("escape", true);
			Window()->PostMessage(&message);
			break;
		}
		case B_FUNCTION_KEY:
		{
			int32 key = 0;
			BMessage* current = Window()->CurrentMessage();
			if (current != NULL && current->FindInt32("key", &key) == B_OK
				&& key == B_F11_KEY) {
				command = kMsgFullScreen;
			}
			break;
		}
		default:
			BView::KeyDown(bytes, numBytes);
			break;
	}

	if (command != 0)
		Window()->PostMessage(command);
}


void
ImageView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_MOUSE_WHEEL_CHANGED:
		{
			float deltaX = message->GetFloat("be:wheel_delta_x", 0.0f);
			float deltaY = message->GetFloat("be:wheel_delta_y", 0.0f);
			uint32 keys = modifiers();

			if ((keys & (B_CONTROL_KEY | B_COMMAND_KEY)) != 0) {
				// zoom around the mouse pointer
				if (deltaY != 0.0f) {
					BPoint where;
					uint32 buttons;
					GetMouse(&where, &buttons, false);
					BMessage zoom(deltaY < 0 ? kMsgZoomIn : kMsgZoomOut);
					zoom.AddPoint("where", where);
					Window()->PostMessage(&zoom);
				}
			} else {
				if ((keys & B_SHIFT_KEY) != 0 && deltaX == 0.0f) {
					deltaX = deltaY;
					deltaY = 0.0f;
				}
				ScrollByDelta(deltaX * kWheelStep, deltaY * kWheelStep);
			}

			if (message->IsSourceWaiting()) {
				BMessage reply(B_REPLY);
				message->SendReply(&reply);
			}
			break;
		}

		default:
			BView::MessageReceived(message);
			break;
	}
}


void
ImageView::MouseDown(BPoint where)
{
	MakeFocus(true);

	int32 buttons = 0;
	int32 clicks = 0;
	BMessage* current = Window()->CurrentMessage();
	if (current != NULL) {
		current->FindInt32("buttons", &buttons);
		current->FindInt32("clicks", &clicks);
	}

	if ((buttons & B_SECONDARY_MOUSE_BUTTON) != 0) {
		_ShowContextMenu(where);
		return;
	}

	if (clicks == 2) {
		Window()->PostMessage(kMsgFullScreen);
		return;
	}

	// pan by dragging
	fDragging = true;
	fDragStart = ConvertToScreen(where);
	fScrollStart = Bounds().LeftTop();
	SetMouseEventMask(B_POINTER_EVENTS, B_NO_POINTER_HISTORY);
	BCursor cursor(B_CURSOR_ID_GRABBING);
	SetViewCursor(&cursor);
}


void
ImageView::MouseMoved(BPoint where, uint32 transit,
	const BMessage* dragMessage)
{
	if (!fDragging)
		return;

	// screen coordinates do not move with the scrolled view
	BPoint screen = ConvertToScreen(where);
	_ScrollToClamped(fScrollStart.x - (screen.x - fDragStart.x),
		fScrollStart.y - (screen.y - fDragStart.y));
}


void
ImageView::MouseUp(BPoint where)
{
	if (!fDragging)
		return;
	fDragging = false;
	SetViewCursor(B_CURSOR_SYSTEM_DEFAULT);
}


void
ImageView::SetImage(const LoadedImagePtr& image, int32 zoomMode,
	float zoomFactor, bool enlargeSmall)
{
	fImage = image;
	fZoomMode = zoomMode;
	fZoomFactor = std::max(kMinZoom, std::min(kMaxZoom, zoomFactor));
	fEnlargeSmall = enlargeSmall;

	BSize viewSize = _ViewSize();
	fLastViewSize = viewSize;
	fScale = _ComputeScale(viewSize);
	_UpdateScrollBars();

	// start at the top, centered horizontally
	BSize size = _ScaledSize(fScale);
	_ScrollToClamped((size.width - viewSize.width) / 2, 0);
	Invalidate();
}


void
ImageView::SetMessage(const char* message)
{
	fMessage = message;
	Invalidate();
}


void
ImageView::SetZoomMode(int32 mode)
{
	_ChangeZoom(mode, fZoomFactor, NULL);
}


void
ImageView::SetZoom(float factor, const BPoint* anchor)
{
	_ChangeZoom(kZoomFixed, std::max(kMinZoom, std::min(kMaxZoom, factor)),
		anchor);
}


void
ImageView::ZoomIn(const BPoint* anchor)
{
	float factor = kMaxZoom;
	for (int32 i = 0; i < kZoomLevelCount; i++) {
		if (kZoomLevels[i] > fScale * 1.01f) {
			factor = kZoomLevels[i];
			break;
		}
	}
	SetZoom(factor, anchor);
}


void
ImageView::ZoomOut(const BPoint* anchor)
{
	float factor = kMinZoom;
	for (int32 i = kZoomLevelCount - 1; i >= 0; i--) {
		if (kZoomLevels[i] < fScale * 0.99f) {
			factor = kZoomLevels[i];
			break;
		}
	}
	SetZoom(factor, anchor);
}


void
ImageView::SetEnlargeSmall(bool enlarge)
{
	fEnlargeSmall = enlarge;
	if (fZoomMode != kZoomFixed)
		_ChangeZoom(fZoomMode, fZoomFactor, NULL);
}


void
ImageView::ScrollByDelta(float x, float y)
{
	BRect bounds = Bounds();
	_ScrollToClamped(bounds.left + x, bounds.top + y);
}


void
ImageView::SetBackground(rgb_color color)
{
	fBackground = color;
	Invalidate();
}


// #pragma mark - private


float
ImageView::_ComputeScale(BSize viewSize) const
{
	if (!fImage || fImage->width <= 0 || fImage->height <= 0)
		return 1.0f;

	float scale;
	switch (fZoomMode) {
		case kZoomFitWindow:
			scale = std::min(viewSize.width / fImage->width,
				viewSize.height / fImage->height);
			if (!fEnlargeSmall)
				scale = std::min(scale, 1.0f);
			break;
		case kZoomFitWidth:
			scale = viewSize.width / fImage->width;
			if (!fEnlargeSmall)
				scale = std::min(scale, 1.0f);
			break;
		default:
			scale = fZoomFactor;
			break;
	}
	return std::max(kMinZoom, std::min(kMaxZoom, scale));
}


BSize
ImageView::_ViewSize() const
{
	BRect bounds = Bounds();
	return BSize(bounds.Width() + 1, bounds.Height() + 1);
}


BSize
ImageView::_ScaledSize(float scale) const
{
	if (!fImage)
		return BSize(0, 0);
	return BSize(std::max(1.0f, roundf(fImage->width * scale)),
		std::max(1.0f, roundf(fImage->height * scale)));
}


BPoint
ImageView::_Origin(float scale, BSize viewSize) const
{
	BSize size = _ScaledSize(scale);
	return BPoint(
		size.width < viewSize.width
			? floorf((viewSize.width - size.width) / 2) : 0,
		size.height < viewSize.height
			? floorf((viewSize.height - size.height) / 2) : 0);
}


BRect
ImageView::_ImageRect() const
{
	BSize size = _ScaledSize(fScale);
	BPoint origin = _Origin(fScale, _ViewSize());
	return BRect(origin.x, origin.y, origin.x + size.width - 1,
		origin.y + size.height - 1);
}


void
ImageView::_UpdateScrollBars()
{
	BSize viewSize = _ViewSize();
	BSize size = _ScaledSize(fScale);

	BScrollBar* scrollBar = ScrollBar(B_HORIZONTAL);
	if (scrollBar != NULL) {
		scrollBar->SetRange(0, std::max(0.0f, size.width - viewSize.width));
		scrollBar->SetProportion(size.width > viewSize.width
			? viewSize.width / size.width : 1.0f);
		scrollBar->SetSteps(kKeyStep,
			std::max(kKeyStep, floorf(viewSize.width * 0.9f)));
	}

	scrollBar = ScrollBar(B_VERTICAL);
	if (scrollBar != NULL) {
		scrollBar->SetRange(0, std::max(0.0f, size.height - viewSize.height));
		scrollBar->SetProportion(size.height > viewSize.height
			? viewSize.height / size.height : 1.0f);
		scrollBar->SetSteps(kKeyStep,
			std::max(kKeyStep, floorf(viewSize.height * 0.9f)));
	}
}


void
ImageView::_ScrollToClamped(float x, float y)
{
	BSize viewSize = _ViewSize();
	BSize size = _ScaledSize(fScale);
	float maxX = std::max(0.0f, size.width - viewSize.width);
	float maxY = std::max(0.0f, size.height - viewSize.height);
	x = roundf(std::max(0.0f, std::min(maxX, x)));
	y = roundf(std::max(0.0f, std::min(maxY, y)));

	BRect bounds = Bounds();
	if (x != bounds.left || y != bounds.top)
		ScrollTo(x, y);
}


void
ImageView::_ChangeZoom(int32 mode, float factor, const BPoint* anchor)
{
	if (!fImage || fImage->Bitmap() == NULL) {
		fZoomMode = mode;
		fZoomFactor = factor;
		return;
	}

	// keep the image point under the anchor (default: the middle) in place
	BSize viewSize = _ViewSize();
	BRect bounds = Bounds();
	BPoint point = anchor != NULL ? *anchor
		: BPoint(bounds.left + viewSize.width / 2,
			bounds.top + viewSize.height / 2);
	BPoint origin = _Origin(fScale, viewSize);
	float imageX = std::max(0.0f, std::min((float)fImage->width,
		(point.x - origin.x) / fScale));
	float imageY = std::max(0.0f, std::min((float)fImage->height,
		(point.y - origin.y) / fScale));
	BPoint relative(point.x - bounds.left, point.y - bounds.top);

	fZoomMode = mode;
	fZoomFactor = factor;
	fScale = _ComputeScale(viewSize);
	_UpdateScrollBars();

	BPoint newOrigin = _Origin(fScale, viewSize);
	_ScrollToClamped(imageX * fScale + newOrigin.x - relative.x,
		imageY * fScale + newOrigin.y - relative.y);
	Invalidate();
}


void
ImageView::_ShowContextMenu(BPoint where)
{
	BPopUpMenu* menu = new BPopUpMenu("context", false, false);

	menu->AddItem(new BMenuItem("Previous image",
		new BMessage(kMsgPreviousImage)));
	menu->AddItem(new BMenuItem("Next image", new BMessage(kMsgNextImage)));
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem("Zoom in", new BMessage(kMsgZoomIn)));
	menu->AddItem(new BMenuItem("Zoom out", new BMessage(kMsgZoomOut)));
	BMenuItem* item = new BMenuItem("Original size",
		new BMessage(kMsgZoomOriginal));
	item->SetMarked(fZoomMode == kZoomFixed && fScale == 1.0f);
	menu->AddItem(item);
	item = new BMenuItem("Fit to window", new BMessage(kMsgZoomFitWindow));
	item->SetMarked(fZoomMode == kZoomFitWindow);
	menu->AddItem(item);
	item = new BMenuItem("Fit width", new BMessage(kMsgZoomFitWidth));
	item->SetMarked(fZoomMode == kZoomFitWidth);
	menu->AddItem(item);
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem("Full screen", new BMessage(kMsgFullScreen)));
	menu->AddItem(new BMenuItem("Show in browser",
		new BMessage(kMsgShowInBrowser)));
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem("Close", new BMessage(B_QUIT_REQUESTED)));

	menu->SetTargetForItems(Window());
	menu->SetAsyncAutoDestruct(true);
	menu->Go(ConvertToScreen(where), true, true, true);
}
