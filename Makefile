CPPFLAGS=-I/usr/include/nodejs -D_FILE_OFFSET_BITS=64 
LDFLAGS=-L../libv8-3.8.9.20 -lv8

OBJS = fuse4js.o

CC = g++

fuse4js.node: fuse4js.o llfuse4js.o
	g++ -shared -o fuse4js.node fuse4js.o llfuse4js.o -lfuse

fuse4js.o: fuse4js.cc
	$(CC) -o fuse4js.o -c fuse4js.cc $(CPPFLAGS) -fPIC

llfuse4js.o: llfuse4js.cc
	$(CC) -o llfuse4js.o -c llfuse4js.cc $(CPPFLAGS) -fPIC

install:
	mkdir -p ~/.node_modules/fuse4js/build/Release
	cp package.json ~/.node_modules/fuse4js
	cp fuse4js.node ~/.node_modules/fuse4js/build/Release

clean:
	$(RM) fuse4js.o llfuse4js.o fuse4js.node
	rm -rf ~/.node_modules/fuse4js
