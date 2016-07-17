LDLIBS = -lsndfile -lsamplerate

ttyplay: main.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(LDLIBS) main.c -o ttyplay

clean:
	rm -f ttyplay *~
