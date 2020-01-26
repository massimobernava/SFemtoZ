all:
	gcc -std=c99 sfemtoz.c -o sfemtoz -pthread -lm -lasound -lsoundio -lsndfile -lwiringPi
