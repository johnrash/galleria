/*
 * Copyright 2026, Galleria authors.
 * Distributed under the terms of the MIT License.
 */


#include "ImageFile.h"

#include <algorithm>
#include <mutex>
#include <new>
#include <set>

#include <pthread.h>
#include <string.h>
#include <sys/stat.h>

#include <Bitmap.h>
#include <BitmapStream.h>
#include <Collator.h>
#include <Directory.h>
#include <File.h>
#include <Locale.h>
#include <Message.h>
#include <MimeType.h>
#include <NodeInfo.h>
#include <TranslatorRoster.h>

#include "Settings.h"


namespace {

struct ExtensionType {
	const char*	extension;
	const char*	mimeType;
};

// Extensions for files that have no file type yet, or whose MIME type the
// database does not map to an extension. Only used when an installed
// translator reads the MIME type.
const ExtensionType kKnownExtensions[] = {
	{ "jpg", "image/jpeg" }, { "jpeg", "image/jpeg" }, { "jpe", "image/jpeg" },
	{ "jfif", "image/jpeg" }, { "png", "image/png" }, { "gif", "image/gif" },
	{ "bmp", "image/bmp" }, { "dib", "image/bmp" }, { "tif", "image/tiff" },
	{ "tiff", "image/tiff" }, { "webp", "image/webp" },
	{ "avif", "image/avif" }, { "tga", "image/x-targa" },
	{ "psd", "image/vnd.adobe.photoshop" },
	{ "psb", "image/vnd.adobe.photoshop" }, { "pcx", "image/x-pcx" },
	{ "ppm", "image/x-portable-pixmap" }, { "pgm", "image/x-portable-pixmap" },
	{ "pbm", "image/x-portable-pixmap" }, { "pnm", "image/x-portable-pixmap" },
	{ "sgi", "image/sgi" }, { "rgb", "image/sgi" }, { "rgba", "image/sgi" },
	{ "bw", "image/sgi" }, { "ico", "image/vnd.microsoft.icon" },
	{ "cur", "image/vnd.microsoft.icon" }, { "icns", "image/icns" },
	{ "jp2", "image/jp2" }, { "j2k", "image/jp2" }, { "jpf", "image/jp2" },
	{ "jpx", "image/jp2" }, { "jpg2", "image/jp2" },
	{ "hvif", "application/x-vnd.Haiku-icon" },
	{ "wbi", "image/x-wonderbrush" }, { "dng", "image/x-vnd.adobe-dng" },
	{ "crw", "image/x-vnd.photo-raw" }, { "cr2", "image/x-vnd.photo-raw" },
	{ "nef", "image/x-vnd.photo-raw" }, { "nrw", "image/x-vnd.photo-raw" },
	{ "arw", "image/x-vnd.photo-raw" }, { "srf", "image/x-vnd.photo-raw" },
	{ "sr2", "image/x-vnd.photo-raw" }, { "orf", "image/x-vnd.photo-raw" },
	{ "raf", "image/x-vnd.photo-raw" }, { "rw2", "image/x-vnd.photo-raw" },
	{ "rwl", "image/x-vnd.photo-raw" }, { "pef", "image/x-vnd.photo-raw" },
	{ "srw", "image/x-vnd.photo-raw" }, { "mrw", "image/x-vnd.photo-raw" },
	{ "x3f", "image/x-vnd.photo-raw" }, { "3fr", "image/x-vnd.photo-raw" },
	{ "erf", "image/x-vnd.photo-raw" }, { "kdc", "image/x-vnd.photo-raw" },
	{ "dcr", "image/x-vnd.photo-raw" }, { "mos", "image/x-vnd.photo-raw" },
};


// The set of readable image types, collected once from the translators.
class ImageTypes {
public:
	static const ImageTypes& Get()
	{
		pthread_once(&sInitOnce, &_Init);
		return *sInstance;
	}

	bool HasMimeType(const char* mimeType) const
	{
		BString type(mimeType);
		type.ToLower();
		return fTypes.find(type) != fTypes.end();
	}

	bool HasExtension(const BString& extension) const
	{
		return fExtensions.find(extension) != fExtensions.end();
	}

private:
	static void _Init()
	{
		sInstance = new ImageTypes;
		sInstance->_Collect();
	}

	void _Collect()
	{
		BTranslatorRoster* roster = BTranslatorRoster::Default();
		translator_id* ids = NULL;
		int32 count = 0;
		if (roster != NULL && roster->GetAllTranslators(&ids, &count) == B_OK) {
			for (int32 i = 0; i < count; i++) {
				const translation_format* formats = NULL;
				int32 formatCount = 0;
				if (roster->GetInputFormats(ids[i], &formats, &formatCount)
						!= B_OK) {
					continue;
				}
				for (int32 j = 0; j < formatCount; j++) {
					if (formats[j].group != B_TRANSLATOR_BITMAP
						|| formats[j].MIME[0] == '\0') {
						continue;
					}
					BString type(formats[j].MIME);
					type.ToLower();
					fTypes.insert(type);
				}
			}
			delete[] ids;
		}

		for (std::set<BString>::const_iterator it = fTypes.begin();
				it != fTypes.end(); it++) {
			BMimeType mimeType(it->String());
			BMessage extensions;
			if (mimeType.GetFileExtensions(&extensions) != B_OK)
				continue;
			const char* extension;
			for (int32 i = 0; extensions.FindString("extensions", i,
					&extension) == B_OK; i++) {
				BString lower(extension[0] == '.' ? extension + 1 : extension);
				lower.ToLower();
				if (!lower.IsEmpty())
					fExtensions.insert(lower);
			}
		}

		for (size_t i = 0; i < sizeof(kKnownExtensions)
				/ sizeof(kKnownExtensions[0]); i++) {
			if (HasMimeType(kKnownExtensions[i].mimeType))
				fExtensions.insert(kKnownExtensions[i].extension);
		}
	}

private:
	std::set<BString>	fTypes;
	std::set<BString>	fExtensions;

	static pthread_once_t	sInitOnce;
	static ImageTypes*		sInstance;
};

pthread_once_t ImageTypes::sInitOnce = PTHREAD_ONCE_INIT;
ImageTypes* ImageTypes::sInstance = NULL;


BString
extension_of(const char* name)
{
	BString extension;
	const char* dot = strrchr(name, '.');
	if (dot != NULL && dot != name && dot[1] != '\0') {
		extension = dot + 1;
		extension.ToLower();
	}
	return extension;
}


int64
to_nanoseconds(const struct timespec& time)
{
	return (int64)time.tv_sec * 1000000000LL + time.tv_nsec;
}


// Decides whether a regular file is a readable image. The extension is
// checked first (no I/O), then the file type attribute, and only files
// without a type are sniffed.
bool
identify_image(const entry_ref& ref, const BString& extension,
	BString& typeKey)
{
	const ImageTypes& types = ImageTypes::Get();
	typeKey = extension;

	if (!extension.IsEmpty() && types.HasExtension(extension))
		return true;

	BNode node(&ref);
	if (node.InitCheck() != B_OK)
		return false;

	char mimeType[B_MIME_TYPE_LENGTH];
	BNodeInfo nodeInfo(&node);
	if (nodeInfo.GetType(mimeType) == B_OK) {
		if (!types.HasMimeType(mimeType))
			return false;
	} else {
		BMimeType guessed;
		if (BMimeType::GuessMimeType(&ref, &guessed) != B_OK
			|| !types.HasMimeType(guessed.Type())) {
			return false;
		}
		strlcpy(mimeType, guessed.Type(), sizeof(mimeType));
	}

	if (typeKey.IsEmpty())
		typeKey = mimeType;
	return true;
}


template<bool kHasAlpha>
void
scale_area_average(const uint8* srcBits, int32 srcBytesPerRow, int32 srcWidth,
	int32 srcHeight, uint8* dstBits, int32 dstBytesPerRow, int32 dstWidth,
	int32 dstHeight)
{
	std::vector<int32> xStart(dstWidth + 1);
	for (int32 x = 0; x <= dstWidth; x++)
		xStart[x] = (int32)((int64)x * srcWidth / dstWidth);

	std::vector<uint64> sums((size_t)dstWidth * 4);

	for (int32 y = 0; y < dstHeight; y++) {
		int32 y0 = (int32)((int64)y * srcHeight / dstHeight);
		int32 y1 = (int32)((int64)(y + 1) * srcHeight / dstHeight);
		if (y1 <= y0)
			y1 = y0 + 1;

		std::fill(sums.begin(), sums.end(), 0);
		for (int32 sy = y0; sy < y1; sy++) {
			const uint8* row = srcBits + (size_t)sy * srcBytesPerRow;
			uint64* sum = sums.data();
			for (int32 x = 0; x < dstWidth; x++, sum += 4) {
				int32 x0 = xStart[x];
				int32 x1 = std::max(xStart[x + 1], x0 + 1);
				const uint8* pixel = row + (size_t)x0 * 4;
				uint64 blue = 0, green = 0, red = 0, alpha = 0;
				for (int32 sx = x0; sx < x1; sx++, pixel += 4) {
					if (kHasAlpha) {
						uint32 weight = pixel[3];
						blue += pixel[0] * weight;
						green += pixel[1] * weight;
						red += pixel[2] * weight;
						alpha += weight;
					} else {
						blue += pixel[0];
						green += pixel[1];
						red += pixel[2];
					}
				}
				sum[0] += blue;
				sum[1] += green;
				sum[2] += red;
				sum[3] += alpha;
			}
		}

		uint8* out = dstBits + (size_t)y * dstBytesPerRow;
		const uint64* sum = sums.data();
		for (int32 x = 0; x < dstWidth; x++, sum += 4, out += 4) {
			int32 x0 = xStart[x];
			int32 x1 = std::max(xStart[x + 1], x0 + 1);
			uint64 count = (uint64)(x1 - x0) * (y1 - y0);
			if (kHasAlpha) {
				uint64 alpha = sum[3];
				if (alpha == 0) {
					out[0] = out[1] = out[2] = out[3] = 0;
					continue;
				}
				out[0] = (uint8)((sum[0] + alpha / 2) / alpha);
				out[1] = (uint8)((sum[1] + alpha / 2) / alpha);
				out[2] = (uint8)((sum[2] + alpha / 2) / alpha);
				out[3] = (uint8)((alpha + count / 2) / count);
			} else {
				out[0] = (uint8)((sum[0] + count / 2) / count);
				out[1] = (uint8)((sum[1] + count / 2) / count);
				out[2] = (uint8)((sum[2] + count / 2) / count);
				out[3] = 255;
			}
		}
	}
}


// Translators return whatever color space suits the format; everything else
// in Galleria works on 32 bit pixels.
BBitmap*
normalize_bitmap(BBitmap* bitmap)
{
	color_space space = bitmap->ColorSpace();
	if (space == B_RGB32 || space == B_RGBA32)
		return bitmap;

	BBitmap* converted = new(std::nothrow) BBitmap(bitmap->Bounds(),
		B_RGBA32);
	if (converted == NULL || converted->InitCheck() != B_OK
		|| converted->ImportBits(bitmap) != B_OK) {
		delete converted;
		converted = NULL;
	}
	delete bitmap;
	return converted;
}


// Translators built on libraries with global state must not run twice at
// the same time; several thumbnail threads decode in parallel.
bool
is_reentrant(BTranslatorRoster* roster, translator_id id)
{
	const char* name = NULL;
	const char* info = NULL;
	int32 version = 0;
	if (roster->GetTranslatorInfo(id, &name, &info, &version) != B_OK
		|| name == NULL) {
		return true;
	}
	return strstr(name, "JPEG2000") == NULL && strstr(name, "RAW") == NULL;
}


std::mutex sSerializedTranslatorLock;


BBitmap*
load_bitmap_from(BFile& file, status_t* _status)
{
	status_t status = file.InitCheck();
	BTranslatorRoster* roster = BTranslatorRoster::Default();
	translator_info info;
	if (status == B_OK) {
		status = roster->Identify(&file, NULL, &info, 0, NULL,
			B_TRANSLATOR_BITMAP);
	}
	if (status == B_OK) {
		BBitmapStream stream;
		if (is_reentrant(roster, info.translator)) {
			status = roster->Translate(&file, &info, NULL, &stream,
				B_TRANSLATOR_BITMAP);
		} else {
			std::lock_guard<std::mutex> lock(sSerializedTranslatorLock);
			status = roster->Translate(&file, &info, NULL, &stream,
				B_TRANSLATOR_BITMAP);
		}
		if (status == B_OK) {
			BBitmap* bitmap = NULL;
			status = stream.DetachBitmap(&bitmap);
			if (status == B_OK && bitmap != NULL) {
				if (bitmap->InitCheck() == B_OK)
					bitmap = normalize_bitmap(bitmap);
				else {
					delete bitmap;
					bitmap = NULL;
				}
				if (bitmap != NULL) {
					if (_status != NULL)
						*_status = B_OK;
					return bitmap;
				}
				status = B_NO_MEMORY;
			} else if (status == B_OK)
				status = B_ERROR;
		}
	}

	if (_status != NULL)
		*_status = status;
	return NULL;
}

}	// namespace


bool
is_supported_image_type(const char* mimeType)
{
	return ImageTypes::Get().HasMimeType(mimeType);
}


status_t
scan_folder(const entry_ref& folder, bool includeFolders, FileList& list)
{
	BDirectory directory(&folder);
	status_t status = directory.InitCheck();
	if (status != B_OK)
		return status;

	entry_ref ref;
	while (directory.GetNextRef(&ref) == B_OK) {
		if (ref.name[0] == '.')
			continue;

		// follow symbolic links, but keep the name of the link
		BEntry entry(&ref, true);
		struct stat st;
		if (entry.GetStat(&st) != B_OK)
			continue;

		FileItem item;
		item.name = ref.name;
		if (S_ISDIR(st.st_mode)) {
			if (!includeFolders)
				continue;
			item.isFolder = true;
		} else if (S_ISREG(st.st_mode)) {
			entry_ref resolved;
			if (entry.GetRef(&resolved) != B_OK
				|| !identify_image(resolved, extension_of(ref.name),
					item.typeKey)) {
				continue;
			}
		} else
			continue;

		if (entry.GetRef(&item.ref) != B_OK)
			continue;
		item.nodeRef.device = st.st_dev;
		item.nodeRef.node = st.st_ino;
		item.size = item.isFolder ? 0 : st.st_size;
		item.modified = to_nanoseconds(st.st_mtim);
		item.created = to_nanoseconds(st.st_crtim);
		list.push_back(item);
	}

	return B_OK;
}


void
init_name_collator(BCollator& collator)
{
	BLocale::Default()->GetCollator(&collator);
	collator.SetNumericSorting(true);
}


int
compare_files(const FileItem& a, const FileItem& b, int32 sortKey,
	bool descending, BCollator& collator)
{
	// folders always come first, whatever the order
	if (a.isFolder != b.isFolder)
		return a.isFolder ? -1 : 1;

	int result = 0;
	switch (sortKey) {
		case kSortByModified:
			result = a.modified < b.modified ? -1
				: a.modified > b.modified ? 1 : 0;
			break;
		case kSortByCreated:
			result = a.created < b.created ? -1
				: a.created > b.created ? 1 : 0;
			break;
		case kSortBySize:
			result = a.size < b.size ? -1 : a.size > b.size ? 1 : 0;
			break;
		case kSortByType:
			result = a.typeKey.Compare(b.typeKey);
			break;
		default:
			break;
	}

	if (result == 0)
		result = collator.Compare(a.name.String(), b.name.String());
	if (result == 0)
		result = strcmp(a.name.String(), b.name.String());

	return descending ? -result : result;
}


void
sort_files(FileList& list, int32 sortKey, bool descending)
{
	BCollator collator;
	init_name_collator(collator);
	std::stable_sort(list.begin(), list.end(),
		[&](const FileItem& a, const FileItem& b) {
			return compare_files(a, b, sortKey, descending, collator) < 0;
		});
}


BBitmap*
load_bitmap(const entry_ref& ref, status_t* _status)
{
	BFile file(&ref, B_READ_ONLY);
	return load_bitmap_from(file, _status);
}


BBitmap*
load_bitmap(const char* path, status_t* _status)
{
	BFile file(path, B_READ_ONLY);
	return load_bitmap_from(file, _status);
}


BBitmap*
scale_bitmap(const BBitmap* source, int32 width, int32 height)
{
	color_space space = source->ColorSpace();
	if ((space != B_RGB32 && space != B_RGBA32) || width < 1 || height < 1)
		return NULL;

	BBitmap* scaled = new(std::nothrow) BBitmap(
		BRect(0, 0, width - 1, height - 1), space);
	if (scaled == NULL || scaled->InitCheck() != B_OK) {
		delete scaled;
		return NULL;
	}

	BRect bounds = source->Bounds();
	int32 srcWidth = bounds.IntegerWidth() + 1;
	int32 srcHeight = bounds.IntegerHeight() + 1;

	if (space == B_RGBA32) {
		scale_area_average<true>((const uint8*)source->Bits(),
			source->BytesPerRow(), srcWidth, srcHeight,
			(uint8*)scaled->Bits(), scaled->BytesPerRow(), width, height);
	} else {
		scale_area_average<false>((const uint8*)source->Bits(),
			source->BytesPerRow(), srcWidth, srcHeight,
			(uint8*)scaled->Bits(), scaled->BytesPerRow(), width, height);
	}
	return scaled;
}


void
fit_size(int32 width, int32 height, int32 maxWidth, int32 maxHeight,
	int32& fitWidth, int32& fitHeight)
{
	if (width <= maxWidth && height <= maxHeight) {
		fitWidth = std::max(width, (int32)1);
		fitHeight = std::max(height, (int32)1);
		return;
	}

	double scale = std::min((double)maxWidth / width,
		(double)maxHeight / height);
	fitWidth = std::max((int32)(width * scale + 0.5), (int32)1);
	fitHeight = std::max((int32)(height * scale + 0.5), (int32)1);
}


BString
format_size(off_t size)
{
	static const char* const kUnits[] = { "KiB", "MiB", "GiB", "TiB" };

	BString string;
	if (size < 1024) {
		string.SetToFormat("%" B_PRIdOFF " bytes", size);
		return string;
	}

	double value = size / 1024.0;
	int unit = 0;
	while (value >= 1024.0 && unit < 3) {
		value /= 1024.0;
		unit++;
	}
	string.SetToFormat("%.1f %s", value, kUnits[unit]);
	return string;
}
