CC = /bin/g++
EXE = http_server console.cgi cgi_server
LINUX_FLAGS=-std=c++14 -Wall -pedantic -pthread -lboost_system
LINUX_INCLUDE_DIRS=/usr/local/include
LINUX_INCLUDE_PARAMS=$(addprefix -I , $(LINUX_INCLUDE_DIRS))
LINUX_LIB_DIRS=/usr/local/lib
LINUX_LIB_PARAMS=$(addprefix -L , $(LINUX_LIB_DIRS))

part1:
	$(CC) http_server.cpp -o http_server $(LINUX_INCLUDE_PARAMS) $(LINUX_LIB_PARAMS) $(LINUX_FLAGS)
	$(CC) console.cpp -o console.cgi $(LINUX_INCLUDE_PARAMS) $(LINUX_LIB_PARAMS) $(LINUX_FLAGS)

part2:
	g++ cgi_server.cpp -o cgi_server -lws2_32 -lwsock32 -std=c++14

clean:
	rm $(EXE)