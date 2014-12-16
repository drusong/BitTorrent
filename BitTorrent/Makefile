CC=gcc
CFLAGS= -Iinclude -Wall  -g -DDEBUG
LDFLAGS=-L./lib -Wl,-rpath=./lib -Wl,-rpath=/usr/local/lib

ttorrent: main.o parse_metafile.o tracker.o bitfield.o sha1.o message.o peer.o data.o policy.o torrent.o bterror.o log.o signal_hander.o
	$(CC) -o $@ $(LDFLAGS) $^ -ldl

clean:
	rm -rf *.o ttorrent
