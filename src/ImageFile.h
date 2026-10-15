/*
 * Copyright 2026, Galleria authors.
 * Distributed under the terms of the MIT License.
 *
 * Image file helpers: which files are images (every bitmap format the
 * installed Translation Kit translators can read), folder scanning, sorting,
 * loading through the translators and high quality downscaling.
 */
#ifndef IMAGE_FILE_H
#define IMAGE_FILE_H


#include <vector>

#include <Entry.h>
#include <Node.h>
#include <String.h>


class BBitmap;
class BCollator;


struct FileItem {
	entry_ref	ref;			// resolved (symlinks traversed)
	node_ref	nodeRef;
	BString		name;			// name as shown in the folder
	BString		typeKey;		// lower case extension or MIME type
	off_t		size;
	int64		modified;		// nanoseconds since the epoch
	int64		created;
	bool		isFolder;

	FileItem()
		:
		size(0),
		modified(0),
		created(0),
		isFolder(false)
	{
	}
};

typedef std::vector<FileItem> FileList;


// True if one of the installed translators reads this MIME type into a
// bitmap.
bool		is_supported_image_type(const char* mimeType);

// Reads the folder: images (and folders when includeFolders is set),
// skipping hidden entries.
status_t	scan_folder(const entry_ref& folder, bool includeFolders,
				FileList& list);

// Comparison used for sorting; folders always come first. The collator is
// set up with init_name_collator() (natural, locale aware name order).
void		init_name_collator(BCollator& collator);
int			compare_files(const FileItem& a, const FileItem& b, int32 sortKey,
				bool descending, BCollator& collator);
void		sort_files(FileList& list, int32 sortKey, bool descending);

// Loads an image with the Translation Kit; the result is always B_RGB32 or
// B_RGBA32. Returns NULL on failure (with the reason in *_status).
BBitmap*	load_bitmap(const entry_ref& ref, status_t* _status = NULL);
BBitmap*	load_bitmap(const char* path, status_t* _status = NULL);

// Area averaging downscale of a B_RGB32/B_RGBA32 bitmap (alpha weighted).
BBitmap*	scale_bitmap(const BBitmap* source, int32 width, int32 height);

// Fits width x height into maxWidth x maxHeight keeping the aspect ratio,
// never enlarging; the results are at least 1.
void		fit_size(int32 width, int32 height, int32 maxWidth,
				int32 maxHeight, int32& fitWidth, int32& fitHeight);

// Human readable size ("3.2 MiB").
BString		format_size(off_t size);


#endif	// IMAGE_FILE_H
