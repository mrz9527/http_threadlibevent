http_server:main.o http_conn.o
	g++ -std=c++11 main.o http_conn.o -o http_server -I /usr/include/event2 -levent_core -lpthread -levent_pthreads

main.o:main.cpp http_conn.o
	g++ -std=c++11 -c main.cpp http_conn.o -o main.o -I /usr/include/event2 -levent_core -lpthread -levent_pthreads

http_conn.o:
	g++ -std=c++11 -c http_conn.cpp -o http_conn.o -I /usr/include/event2 -levent_core -lpthread -levent_pthreads

clean:
	rm -rf *.o http_server
