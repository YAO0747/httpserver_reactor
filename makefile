# server : main.o pub.o http_conn.o
#	g++ -g -o server main.o pub.o http_conn.o

#main.o : main.c pub.o http_conn.o
#	g++ -g -c main.c -o main.o

#http_conn.o : ./http_conn/http_conn.cpp pub.o
#	g++ -g -c ./http_conn/http_conn.cpp -o http_conn.o

#pub.o : ./pub/pub.cpp
#	g++ -g -c ./pub/pub.cpp -o pub.o

#clean :
#	rm server main.o pub.o http_conn.o

server : main.c ./http_conn/http_conn.cpp ./pub/pub.cpp ./threadpool/threadpool.h ./log/log.h ./log/log.cpp ./log/block_queue.h
	g++ -g -pthread -o server main.c ./http_conn/http_conn.cpp ./pub/pub.cpp ./log/log.cpp

clean :
	rm server
