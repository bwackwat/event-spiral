#include "util.hpp"
#include "tcp-event-server.hpp"
#include "private-event-server.hpp"

PrivateEventServer::PrivateEventServer(std::string certificate, std::string private_key, uint16_t port, size_t max_connections)
:EventServer("PrivateEventServer", port, max_connections){
	SSL_library_init();
	SSL_load_error_strings();

        if((this->ctx = SSL_CTX_new(SSLv23_server_method())) == 0){
                ERR_print_errors_fp(stdout);
		throw new std::runtime_error("SSL_CTX_new");
        }
        if(SSL_CTX_set_ecdh_auto(this->ctx, 1) == 0){
                ERR_print_errors_fp(stdout);
		throw new std::runtime_error("SSL_CTX_set_ecdh_auto");
	}
        if(SSL_CTX_use_certificate_file(this->ctx, certificate.c_str(), SSL_FILETYPE_PEM) != 1){
                ERR_print_errors_fp(stdout);
		throw new std::runtime_error("SSL_CTX_use_certificate_file");
        }
        if(SSL_CTX_use_PrivateKey_file(this->ctx, private_key.c_str(), SSL_FILETYPE_PEM) != 1){
                ERR_print_errors_fp(stdout);
		throw new std::runtime_error("SSL_CTX_use_PrivateKey_file");
        }

//	PRINT("SSL Mode: " << SSL_CTX_set_mode(this->ctx, SSL_MODE_ENABLE_PARTIAL_WRITE))
}

// Basically does an SSL_free before closing the socket.
void PrivateEventServer::close_client(size_t index){
	SSL_free(this->client_ssl[this->client_fds[index]]);
	EventServer::close_client(index);
}

bool PrivateEventServer::send(int fd, const char* data, size_t data_length){
	int len = SSL_write(this->client_ssl[fd], data, static_cast<int>(data_length));
	switch(SSL_get_error(this->client_ssl[fd], len)){
	case SSL_ERROR_NONE:
		break;
	default:
		ERROR("SSL_write")
		return true;
	}
	if(len != data_length){
		ERROR("Invalid number of bytes written...")
		return true;
	}
	return false;
}

bool PrivateEventServer::recv(int fd, char* data, size_t data_length){
	int len;
	len = SSL_read(this->client_ssl[fd], data, static_cast<int>(data_length));
	switch(SSL_get_error(this->client_ssl[fd], len)){
	case SSL_ERROR_NONE:
		break;
	case SSL_ERROR_WANT_READ:
		// Nothing to read, nonblocking mode.
		return false;
	case SSL_ERROR_ZERO_RETURN:
		ERROR("server read zero")
		return true;
	default:
		ERROR("other SSL_read")
		return true;
	}
	data[len] = 0;
	return packet_received(fd, data, len);
}

bool PrivateEventServer::non_blocking_accept(){
	int new_client_fd;
	size_t new_fd_index;
	if((new_client_fd = accept(this->server_fd, 0, 0)) < 0){
		if(errno != EWOULDBLOCK){
			ERROR("accept")
			return false;
		}
		// Nothing to accept = ^_^
		return true;
	}

//	SSL_accept fails if socket is non_blocking.
//	Util::set_non_blocking(new_client_fd);

	new_fd_index = this->next_fds->pop();
	this->client_fds[new_fd_index] = new_client_fd;
	PRINT("CONNECTION " << new_fd_index << " FROM " << new_client_fd)

	if((this->client_ssl[new_client_fd] = SSL_new(this->ctx)) == 0){
		ERROR("SSL_new")
		ERR_print_errors_fp(stdout);
		this->close_client(new_fd_index);
		return true;
	}

	SSL_set_fd(this->client_ssl[new_client_fd], new_client_fd);

	if(SSL_accept(this->client_ssl[new_client_fd]) <= 0){
		ERROR("SSL_accept")
		ERR_print_errors_fp(stdout);
		this->close_client(new_fd_index);
		return true;
	}

	Util::set_non_blocking(new_client_fd);

	if(this->on_connect != nullptr){
		this->on_connect(new_client_fd);
	}
	return true;
}

PrivateEventServer::~PrivateEventServer(){
	SSL_CTX_free(this->ctx);
	EVP_cleanup();
}
