all:
	gcc -std=c99 sfemtoz.c -o sfemtoz -lsoundio -lsndfile -lwiringPi
