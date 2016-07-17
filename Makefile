ttyplay: main.c
	gcc -O3 -Wall -o ttyplay main.c -lsndfile -lsamplerate

clean:
	rm -f ttyplay *~
