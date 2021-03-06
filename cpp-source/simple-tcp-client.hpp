#pragma once

#include <stdexcept>
#include <string>
#include <cstring>
#include <iostream>
#include <csignal>
#include <typeinfo>
#include <atomic>
#include <cstdio>

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <termios.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>

class SimpleTcpClient{
protected:
	uint16_t port;
	std::string name;
	bool verbose;
	struct in_addr remote_address;
public:
	std::atomic<bool> connected;
	SimpleTcpClient(std::string hostname, uint16_t new_port, bool new_verbose = false);
	SimpleTcpClient(struct in_addr new_remote_address, uint16_t new_port, bool new_verbose = false);
	SimpleTcpClient(const char* ip_address, uint16_t new_port, bool new_verbose = false);

	bool communicate(const char* request, size_t length, char* response);
	bool reconnect();
	~SimpleTcpClient();

	int fd;
	int requests;
};
