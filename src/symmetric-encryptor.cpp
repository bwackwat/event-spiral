#include <fcntl.h>

#include "util.hpp"
#include "symmetric-encryptor.hpp"

SymmetricEncryptor::SymmetricEncryptor(){
	this->random_pool.GenerateBlock(this->key, CryptoPP::AES::MAX_KEYLENGTH);
	this->random_pool.GenerateBlock(this->iv, CryptoPP::AES::BLOCKSIZE);
}

std::string SymmetricEncryptor::encrypt(std::string data){
	std::string new_data;
	CryptoPP::StringSink* sink = new CryptoPP::StringSink(new_data);
	CryptoPP::Base64Encoder* base64_enc = new CryptoPP::Base64Encoder(sink, false);
	CryptoPP::CBC_Mode<CryptoPP::AES>::Encryption enc(this->key, CryptoPP::AES::MAX_KEYLENGTH, this->iv);
	CryptoPP::StreamTransformationFilter* aes_enc = new CryptoPP::StreamTransformationFilter(enc, base64_enc);
	CryptoPP::StringSource enc_source(data, true, aes_enc);
	return new_data;
}

std::string SymmetricEncryptor::decrypt(std::string data){
	std::string new_data;
	CryptoPP::StringSink* sink = new CryptoPP::StringSink(new_data);
	CryptoPP::CBC_Mode<CryptoPP::AES>::Decryption dec(this->key, CryptoPP::AES::MAX_KEYLENGTH, this->iv);
	CryptoPP::StreamTransformationFilter* aes_dec = new CryptoPP::StreamTransformationFilter(dec, sink);
	CryptoPP::Base64Decoder* base64_dec = new CryptoPP::Base64Decoder(aes_dec);
	CryptoPP::StringSource dec_source(data, true, base64_dec);
	return new_data;
}

SymmetricEncryptor::SymmetricEncryptor(std::string keyfile){
	std::string name = "SymmetricEncryptor";
	int fd;
	if((fd = open(keyfile.c_str(), O_RDONLY)) < 0){
		throw std::runtime_error(name + " open keyfile");
	}
	if(read(fd, this->key, CryptoPP::AES::MAX_KEYLENGTH) < CryptoPP::AES::MAX_KEYLENGTH){
		throw std::runtime_error(name + " read keyfile key");
	}
	if(read(fd, this->iv, CryptoPP::AES::BLOCKSIZE) < CryptoPP::AES::BLOCKSIZE){
		throw std::runtime_error(name + " read keyfile iv");
	}
	if(close(fd) < 0){
		throw std::runtime_error(name + " close keyfile");
	}
	//srand(static_cast<unsigned int>(time(0)));
}

std::string SymmetricEncryptor::encrypt(std::string data, int transaction){
	byte salt[CryptoPP::AES::BLOCKSIZE];
	this->random_pool.GenerateBlock(salt, CryptoPP::AES::BLOCKSIZE);
	std::string salts(reinterpret_cast<const char*>(salt), CryptoPP::AES::BLOCKSIZE);

	return this->encrypt(static_cast<char>(transaction % 255) + salts + data);
}

std::string SymmetricEncryptor::decrypt(std::string data, int transaction){
	std::string new_data = this->decrypt(data);

	if(static_cast<unsigned char>(new_data[0]) != static_cast<unsigned char>(transaction % 255)){
		PRINT("got " << static_cast<int>(new_data[0]) << " expected " << transaction)
		throw CryptoPP::Exception(CryptoPP::BERDecodeErr::INVALID_DATA_FORMAT, "Bad transaction number.");
	}
	new_data.erase(0, CryptoPP::AES::BLOCKSIZE + 1);
	return new_data;
}

bool SymmetricEncryptor::send(int fd, const char* data, size_t /*data_length*/, int transaction){
	std::string send_data = this->encrypt(data, transaction);
	ssize_t len;
	PRINT("SDATA|" << send_data << '|')
	if((len = write(fd, send_data.c_str(), send_data.length())) < 0){
		ERROR("encryptor write")
		return true;
	}else if(len != static_cast<ssize_t>(send_data.length())){
		ERROR("encryptor didn't write all")
		return true;
	}
	return false;
}

/**
 * 1. Reads uint32_t for the size of the encrypted block.
 * 2. Reads that size for the encrypted block.
 * 3. Decrypts the block.
 * 4. Starts back at 1, and tries to read another uint32_t in case many messages merged.
 * 
 * Are TCP messages merging a consequence of EPOLLONESHOT? Even if it is, I still prefer it.
 */
ssize_t SymmetricEncryptor::recv(int fd, char* data, size_t data_length,
std::function<ssize_t(int, const char*, size_t)> callback, int transaction){
	ssize_t len;
	std::string recv_data;
	if((len = read(fd, data, data_length)) < 0){
		if(errno != EWOULDBLOCK){
			perror("encryptor read");
			ERROR("encryptor read " << fd)
			return -1;
		}
		return 0;
	}else if(len == 0){
		ERROR("encryptor read zero")
		return -2;
	}else{
		data[len] = 0;
		PRINT("RDATA|" << data << '|')
		try{
			recv_data = this->decrypt(std::string(data), transaction);
		}catch(const std::exception& e){
			ERROR(e.what() << "\nImplied hacker, closing!")
			return -3;
		}
		if(callback != nullptr){
			return callback(fd, recv_data.c_str(), recv_data.length());
		}
		std::strcpy(data, recv_data.c_str());
		return static_cast<ssize_t>(recv_data.length());
	}
}
