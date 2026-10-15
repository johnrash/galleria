/*
 * Copyright 2026, Galleria authors.
 * Distributed under the terms of the MIT License.
 *
 * Constants and message codes shared by all parts of Galleria.
 */
#ifndef GALLERIA_H
#define GALLERIA_H


#include <Message.h>
#include <SupportDefs.h>


#define kApplicationSignature	"application/x-vnd.Galleria"
#define kApplicationName		"Galleria"

static const int32 kMinThumbnailSize = 48;
static const int32 kMaxThumbnailSize = 512;
static const int32 kDefaultThumbnailSize = 160;
static const int32 kThumbnailSizeStep = 32;


enum {
	// application
	kMsgOpenViewer			= 'Gopv',	// "refs", "index", "newWindow",
										// "scanFolder", "browser"
	kMsgOpenBrowser			= 'Gopb',	// "folder", optional "select"
	kMsgWindowClosed		= 'Gwcl',

	// browser window
	kMsgOpenFolderPanel		= 'Gofp',
	kMsgFolderChosen		= 'Gfch',
	kMsgGoToFolder			= 'Ggto',	// "path" (scripting)
	kMsgParentFolder		= 'Gpar',
	kMsgLocationEntered		= 'Gloc',
	kMsgReload				= 'Grld',
	kMsgRescan				= 'Gres',
	kMsgScanDone			= 'Gscd',
	kMsgSortBy				= 'Gsby',	// "key"
	kMsgSortOrder			= 'Gsor',	// "descending"
	kMsgShowFolders			= 'Gshf',
	kMsgThumbSize			= 'Gtsz',	// from the slider
	kMsgThumbSizeStep		= 'Gtst',	// "delta" (+1/-1)
	kMsgSetThumbSize		= 'Gtss',	// "size" (scripting)
	kMsgClearCache			= 'Gclc',
	kMsgClearCacheConfirmed	= 'Gccc',	// "which" == 1 deletes
	kMsgOpenItem			= 'Gopi',	// "index", "newWindow"
	kMsgItemSelected		= 'Gsel',
	kMsgSelectIndex			= 'Gsli',	// "index" (scripting)
	kMsgSelectRef			= 'Gslr',	// "ref"
	kMsgShowInTracker		= 'Gtrk',	// "index"
	kMsgActivate			= 'Gact',
	kMsgThumbnailReady		= 'Gthr',
	kMsgUpdateRequests		= 'Gupr',

	// viewer window
	kMsgNextImage			= 'Gnxt',
	kMsgPreviousImage		= 'Gprv',
	kMsgFirstImage			= 'Gfst',
	kMsgLastImage			= 'Glst',
	kMsgZoomIn				= 'Gzin',	// optional "where" (view coordinates)
	kMsgZoomOut				= 'Gzou',
	kMsgZoomOriginal		= 'Gz11',
	kMsgZoomFitWindow		= 'Gzfw',
	kMsgZoomFitWidth		= 'Gzfx',
	kMsgEnlargeSmall		= 'Genl',
	kMsgFullScreen			= 'Gfsc',
	kMsgShowInBrowser		= 'Gsib',
	kMsgImageLoaded			= 'Gild',
	kMsgFolderScanned		= 'Gfsd',
	kMsgScrollBy			= 'Gscr',	// "x", "y" (scripting)
	kMsgScaleChanged		= 'Gscc',	// the fit modes follow the window size

	// both windows: replies with the window state (for scripts and tests)
	kMsgGetState			= 'Gsta'
};


// Commands can also be sent with hey, which waits for a reply: answer after
// the command has been carried out, so that scripts can check the result.
static inline void
reply_if_waiting(BMessage* message)
{
	if (message->IsSourceWaiting()) {
		BMessage reply(B_REPLY);
		reply.AddInt32("error", B_OK);
		message->SendReply(&reply);
	}
}


#endif	// GALLERIA_H
