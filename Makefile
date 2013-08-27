all:
	gcc -g -o tcp_server_framework_test main.c tcp_server_framework.c -lsocket -lnsl -lresolv -lpthread
clean:
	rm ./tcp_server_framework_test
