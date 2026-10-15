## Haiku Generic Makefile v2.6 ##
## Build natively on Haiku (needs haiku_devel): make

NAME = Galleria
TYPE = APP
APP_MIME_SIG = application/x-vnd.Galleria

SRCS = \
	src/App.cpp \
	src/BrowserWindow.cpp \
	src/ImageFile.cpp \
	src/ImageLoader.cpp \
	src/ImageView.cpp \
	src/Settings.cpp \
	src/ThumbnailLoader.cpp \
	src/ThumbnailView.cpp \
	src/ViewerWindow.cpp \
	src/WorkerThread.cpp \
	src/ZoomableWindow.cpp

RDEFS = Galleria.rdef

LIBS = be translation tracker $(STDCPPLIBS)

LOCAL_INCLUDE_PATHS = src
# BAboutWindow (exported by libbe, header still in the private tree)
SYSTEM_INCLUDE_PATHS = /boot/system/develop/headers/private/interface
OPTIMIZE := FULL
WARNINGS = ALL
DEBUGGER :=

DEVEL_DIRECTORY := /boot/system/develop
include $(DEVEL_DIRECTORY)/etc/makefile-engine
