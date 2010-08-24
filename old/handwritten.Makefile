# yes, this should be integrated with Mutter's own build system
# and it will be when I get around to it

# oh dear, not elegant
MUTTER_PLUGIN_API_VERSION = 3
MUTTER_MAJOR_VERSION = 2
MUTTER_MINOR_VERSION = 31
MUTTER_MICRO_VERSION = 4

all: xzibit.so xzibit-plugin.o xzibit-rfb-client chicken-man autoshare type-a-key click-the-mouse

xzibit.so: xzibit-plugin.o vnc.o
	gcc -lvncserver -lXtst `pkg-config --libs gtk+-2.0 gdk-2.0` -shared -o xzibit.so xzibit-plugin.o vnc.o

vnc.o: vnc.c vnc.h
	gcc -c `pkg-config --cflags --libs gtk+-2.0` vnc.c

xzibit-plugin.o: xzibit-plugin.c
	gcc -c -fpic -I../src/include `pkg-config --cflags --libs clutter-1.0 gmodule-2.0 gdk-2.0` -DMUTTER_PLUGIN_API_VERSION=$(MUTTER_PLUGIN_API_VERSION) -DMUTTER_MAJOR_VERSION=$(MUTTER_MAJOR_VERSION) -DMUTTER_MINOR_VERSION=$(MUTTER_MINOR_VERSION) -DMUTTER_MICRO_VERSION=$(MUTTER_MICRO_VERSION) xzibit-plugin.c

install: xzibit.so xzibit-rfb-client
	cp xzibit.so /usr/local/lib/mutter/plugins/
	cp xzibit-rfb-client /usr/local/bin

xzibit-rfb-client: xzibit-rfb-client.c
	gcc `pkg-config --cflags --libs gtk+-2.0 gtk-vnc-1.0 gstreamer-0.10` xzibit-rfb-client.c -o xzibit-rfb-client

chicken-man: chicken-man.c
	gcc chicken-man.c `pkg-config --libs --cflags gtk+-2.0 gnutls gdk-pixbuf-2.0` -o chicken-man -ljpeg -lvncserver

autoshare: autoshare.c
	gcc -g autoshare.c `pkg-config --libs --cflags gtk+-2.0` -o autoshare

type-a-key: type-a-key.c
	gcc type-a-key.c -o type-a-key -lX11 -lXtst

click-the-mouse: click-the-mouse.c
	gcc click-the-mouse.c -o click-the-mouse -lX11 -lXtst
