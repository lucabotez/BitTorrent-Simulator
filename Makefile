# Copyright @lucabotez

build:
	mpicc -o bittorrent bittorrent.c -pthread -Wall

clean:
	rm -rf bittorrent
