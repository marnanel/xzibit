all: jupiter orbit

jupiter: jupiter.c xzibit-client.h xzibit-client.c
	gcc `pkg-config --cflags --libs gtk+-2.0` jupiter.c xzibit-client.c -o jupiter -I. -g -lvncserver

orbit: orbit.c xzibit-client.h xzibit-client.c
	gcc `pkg-config --cflags --libs gtk+-2.0` orbit.c xzibit-client.c -o orbit -I. -g -lvncserver

wall: wall.c xzibit-client.h xzibit-client.c
	gcc `pkg-config --cflags --libs gtk+-2.0` wall.c xzibit-client.c -o wall -I. -g -lvncserver
