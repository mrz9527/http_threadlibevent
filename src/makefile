http_server:main.o http_conn.o
	g++ -std=c++11 main.o http_conn.o -o http_server -I./libevent/include -I ./libevent/include/event2 -L./libevent/lib -levent_core -lpthread -levent_pthreads -Wl,-rpath,./libevent/lib

main.o:main.cpp http_conn.o
	g++ -std=c++11 -c main.cpp http_conn.o -o main.o -I./libevent/include -I ./libevent/include/event2 -L./libevent/lib -levent_core -lpthread -levent_pthreads -Wl,-rpath,./libevent/lib

http_conn.o:
	g++ -std=c++11 -c http_conn.cpp -o http_conn.o -I./libevent/include -I ./libevent/include/event2 -L./libevent/lib -levent_core -lpthread -levent_pthreads -Wl,-rpath,./libevent/lib

clean:
	rm -rf *.o http_server
