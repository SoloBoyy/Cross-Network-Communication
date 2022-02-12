# Cross-Network-Communication

This is a C++ assignment goal is to provide communication between the client and server across the network using the TCP/IP protocol. 

Specifically, the client-side end of a request channel will reside on one machine, and the server-side end of the channel on a different machine.
Since the communication API (not just the underlying functionality and features) through TCP/IP is different from FIFO, we are able to adapt the 
system to accpet port numbers and IP's, to faciliate this cross network connection.  I also made sure to make the server dynamic, in the sense that it can 
handle multiple request channels from the client residing on a different machine.

This project was part of my operating systems class, and was recently moved to my public repo.

## Background

Our client.cpp that consists of p patient threads, w worker threads, h histogram threads, and accomplishes file transfer using TCP/IP request channels.

Skeleton code for TCPRequest Channel
    class TCPRequestChannel{
        private:
        /* Since a TCP socket is full-duplex, we need only one.
        This is unlike FIFO that needed one read fd and another
        for write from each side  */
        int sockfd;
        public:
        /* Constructor takes 2 arguments: hostname and port number
        If the host name is an empty string, set up the channel for
        the server side. If the name is non-empty, the constructor
        works for the client side. Both constructors prepare the
        sockfd in the respective way so that it can work as a
        server or client communication endpoint*/
        TCPRequestChannel (const string hostname, const string
        port_no);
        /* This is used by the server to create a channel out of a
        newly accepted client socket. Note that an accepted client
        socket is ready for communication */
        TCPRequestChannel (int sockfd);
        /* destructor */
        ~TCPRequestChannel();
        int cread (void* msgbuf, int buflen);
        int cwrite(void* msgbuf , int msglen);
        /* return the socket file descriptor */
        int getfd();
  };
              
## Example Commands

* Start Server with ./server -r <port no> -m <bufcap>
* Reveal Server IP address with ngrok, ifconfig, ip addr | grep eth0
* ./client -n <# data items> -w <# workers> -b <bb size> -p <# patients>
-h <# histograms> -m <bufcap> -a <hostname / IP address> -r <port no>
* ./client -w <# workers> -b <bb size> -f <filename> -m <bufcap> -a
<hostname / IP address> -r <port no>

