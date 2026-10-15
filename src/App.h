/*
 * Copyright 2026, Galleria authors.
 * Distributed under the terms of the MIT License.
 */
#ifndef APP_H
#define APP_H


#include <Application.h>
#include <Messenger.h>


class App : public BApplication {
public:
								App();

	virtual	void				ArgvReceived(int32 argc, char** argv);
	virtual	void				RefsReceived(BMessage* message);
	virtual	void				ReadyToRun();
	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();
	virtual	void				AboutRequested();

private:
			void				_OpenBrowser(const entry_ref& folder,
									const entry_ref* select = NULL);
			void				_OpenViewer(BMessage* message);
			BRect				_WindowFrame(BRect saved, bool savedZoomed,
									float width, float height,
									bool* _zoom) const;

private:
			int32				fWindowCount;
			BMessenger			fViewer;
};


#endif	// APP_H
