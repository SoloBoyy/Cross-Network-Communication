
#ifndef _TCPreqchannel_H_
#define _TCPreqchannel_H_

#include "common.h"

class TCPRequestChannel
{

private:
	/*  The current implementation uses named pipes. */
	
	int sockfd;

	
public:
	TCPRequestChannel(const string host_name, const string port_no);

	TCPRequestChannel(int fd) {
		sockfd = fd;
	}

	~TCPRequestChannel();

	int cread (void* msgbuf, int bufcapacity);
	
	int cwrite(void *msgbuf , int msglen);
	
    int getfd() {
        return sockfd;
    }
};

#endif
