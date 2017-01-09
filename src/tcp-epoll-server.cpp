#include "util.hpp"
#include "stack.hpp"
#include "tcp-epoll-server.hpp"

EpollServer::EpollServer(uint16_t port, size_t new_max_connections, std::string new_name)
:EventServer(port, new_max_connections, new_name),
client_events(new struct epoll_event[this->max_connections]),
num_connections(0){
	if((this->epoll_fd = epoll_create1(0)) < 0){
		perror("epoll_create1");
		throw std::runtime_error(this->name + " epoll_create1");
	}
	struct epoll_event next_event;
	next_event.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
	next_event.data.fd = this->server_fd;
	if(epoll_ctl(this->epoll_fd, EPOLL_CTL_ADD, this->server_fd, &next_event) < 0){
		perror("epoll_ctl");
		throw std::runtime_error(this->name + " epoll_ctl");
	}
}

ssize_t EpollServer::recv(int fd, char* data, size_t data_length){
	ssize_t len;
	if((len = read(fd, data, data_length)) <= 0){
		PRINT("read error, " << fd << " done")
		return -1;
	}else{
		data[len] = 0;
		return this->on_read(fd, data, len);
	}
}

void EpollServer::run_thread(unsigned int thread_id){
	int num_fds, new_fd, i;
	char packet[PACKET_LIMIT];
	ssize_t len;
	bool running = true;

	PRINT("accept on : " << this->server_fd << " epoll : " << this->epoll_fd)
	while(running){
		if((num_fds = epoll_wait(this->epoll_fd, this->client_events, static_cast<int>(this->max_connections), -1)) < 0){
			perror("epoll_wait");
			running = false;
			continue;
		}
		for(i = 0; i < num_fds; ++i){
			PRINT("check " << this->client_events[i].data.fd << " on " << thread_id)
			if(this->client_events[i].data.fd == this->server_fd){
				if((new_fd = accept(this->server_fd, 0, 0)) < 0){
					perror("accept");
					running = false;
					break;
				}
				PRINT("Accepted " << new_fd << " on " << thread_id)
				// Util::set_non_blocking(new_fd);
				struct epoll_event next_event;
				next_event.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
				next_event.data.fd = new_fd;
				if(epoll_ctl(this->epoll_fd, EPOLL_CTL_ADD, new_fd, &next_event) < 0){
					perror("epoll_ctl add client");
					close(new_fd);
				}else{
					this->num_connections++;
					if(this->on_connect != nullptr){
						this->on_connect(new_fd);
					}
				}
				if(this->num_connections < this->max_connections){
					this->client_events[i].events = EPOLLIN | EPOLLET | EPOLLONESHOT;
					this->client_events[i].data.fd = this->server_fd;
					if(epoll_ctl(this->epoll_fd, EPOLL_CTL_MOD, this->client_events[i].data.fd, &this->client_events[i]) < 0){
						perror("epoll_ctl mod server");
					}
				}else{
					if(epoll_ctl(this->epoll_fd, EPOLL_CTL_DEL, this->client_events[i].data.fd, &this->client_events[i]) < 0){
						perror("epoll_ctl del server");
					}
				}
			}else{
				PRINT("Try read from " << this->client_events[i].data.fd << " on " << thread_id)
				if((len = this->recv(this->client_events[i].data.fd, packet, PACKET_LIMIT)) < 0){
					PRINT("...Thread : " << thread_id)
					if(this->on_disconnect != nullptr){
						this->on_disconnect(this->client_events[i].data.fd);
					}
					if(close(this->client_events[i].data.fd) < 0){
						PRINT("CLOSE " << this->client_events[i].data.fd)
						perror("close client");
						sleep(1);
					}
					//if(epoll_ctl(this->epoll_fd, EPOLL_CTL_DEL, this->client_events[i].data.fd, &this->client_events[i]) < 0){
					//	perror("epoll_ctl del client");
					//}
					if(this->num_connections >= this->max_connections){
						struct epoll_event next_event;
						next_event.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
						next_event.data.fd = this->server_fd;
						if(epoll_ctl(this->epoll_fd, EPOLL_CTL_ADD, this->server_fd, &next_event) < 0){
							perror("epoll_ctl add server");
						}
					}
					this->num_connections--;
				}else if(len == PACKET_LIMIT){
					ERROR("more to read uh oh!")
				}else{
					this->client_events[i].events = EPOLLIN | EPOLLET | EPOLLONESHOT;
					if(epoll_ctl(this->epoll_fd, EPOLL_CTL_MOD, this->client_events[i].data.fd, &this->client_events[i]) < 0){
					 	perror("epoll_ctl mod client");
					}
				}
			}
		}
	}
}