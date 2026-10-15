/*
 * Test helper: types keys into a window of a running application.
 *   sendkey <app signature> <window index> <key> [<key> ...]
 * A key is a single character or one of: left right up down home end pageup
 * pagedown enter backspace escape space.
 *
 * hey cannot do this: a scripted B_KEY_DOWN goes to the scripting code of
 * the view. Like the input server, this sends the key to the window's
 * preferred handler, so the window routes it to its focus view (shortcuts,
 * KeyDown()).
 *
 * Build: g++ -I/boot/system/develop/headers/private/app -o sendkey \
 *            sendkey.cpp -lbe
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <InterfaceDefs.h>
#include <Message.h>
#include <Messenger.h>
#include <OS.h>

#include <MessengerPrivate.h>
#include <TokenSpace.h>


struct NamedKey {
	const char*	name;
	char		byte;
};

static const NamedKey kKeys[] = {
	{ "left", B_LEFT_ARROW }, { "right", B_RIGHT_ARROW },
	{ "up", B_UP_ARROW }, { "down", B_DOWN_ARROW }, { "home", B_HOME },
	{ "end", B_END }, { "pageup", B_PAGE_UP }, { "pagedown", B_PAGE_DOWN },
	{ "enter", B_ENTER }, { "backspace", B_BACKSPACE },
	{ "escape", B_ESCAPE }, { "space", B_SPACE },
};


int
main(int argc, char** argv)
{
	if (argc < 4) {
		fprintf(stderr, "usage: %s <signature> <window index> <key>...\n",
			argv[0]);
		return 1;
	}

	BMessenger application(argv[1]);
	BMessage request(B_GET_PROPERTY);
	request.AddSpecifier("Messenger");
	request.AddSpecifier("Window", (int32)atoi(argv[2]));
	BMessage reply;
	BMessenger window;
	if (application.SendMessage(&request, &reply) != B_OK
		|| reply.FindMessenger("result", &window) != B_OK) {
		fprintf(stderr, "%s: no such window\n", argv[0]);
		return 1;
	}
	BMessenger::Private messenger(window);
	messenger.SetTo(messenger.Team(), messenger.Port(), B_PREFERRED_TOKEN);

	for (int i = 3; i < argc; i++) {
		char bytes[2] = { argv[i][0], '\0' };
		for (size_t k = 0; k < sizeof(kKeys) / sizeof(kKeys[0]); k++) {
			if (strcmp(argv[i], kKeys[k].name) == 0)
				bytes[0] = kKeys[k].byte;
		}

		BMessage key(B_KEY_DOWN);
		key.AddInt64("when", system_time());
		key.AddInt32("modifiers", 0);
		key.AddInt32("key", 0);
		key.AddInt32("raw_char", bytes[0]);
		key.AddInt8("byte", bytes[0]);
		key.AddString("bytes", bytes);
		if (window.SendMessage(&key) != B_OK) {
			fprintf(stderr, "%s: could not send\n", argv[0]);
			return 1;
		}
	}
	return 0;
}
