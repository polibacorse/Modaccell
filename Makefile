modaccel: modaccel.c
	gcc -o modaccel modaccel.c -lmosquitto -ljson-c -lwiringpi -lpthread
