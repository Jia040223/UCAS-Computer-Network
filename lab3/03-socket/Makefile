all: http-server

http-server: *.c
	gcc -Wall -g *.c -o http-server -lssl -lcrypto -lpthread

clean:
	@rm http-server
