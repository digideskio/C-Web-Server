all: build/webserv

build/webserv: src/webserv.c
	@gcc -o $@ $^


clean:
	@rm build/webserv
