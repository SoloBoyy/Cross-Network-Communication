#include "common.h"
#include "BoundedBuffer.h"
#include "Histogram.h"
#include "common.h"
#include "HistogramCollection.h"
#include "TCPreqchannel.h"

#include <sys/epoll.h>
#include <sys/wait.h>
#include <unordered_map>
#include <fcntl.h>
#include <thread>

using namespace std;

void timediff(struct timeval& start, struct timeval& end){
    int secs = (end.tv_sec * 1e6 + end.tv_usec - start.tv_sec * 1e6 - start.tv_usec)/(int) 1e6;
    int usecs = (int)(end.tv_sec * 1e6 + end.tv_usec - start.tv_sec * 1e6 - start.tv_usec)%((int) 1e6);
    cout << "Took " << secs << " seconds and " << usecs << " micro seconds" << endl;

}

void patient_thread_function(int n, int pno, BoundedBuffer* request_buffer){
    datamsg d(pno, 0.0,1);
    double resp = 0;
    for(int i = 0; i < n; i++) {
        request_buffer->push((char*)&d, sizeof(datamsg));
        d.seconds += 0.004;
    }
}

void worker_thread_function(TCPRequestChannel* chan, BoundedBuffer* request_buffer, HistogramCollection* hc, int mb){
    char buf[1024];
    double resp = 0;

    char recvbuf[mb];
    while(true) {
        request_buffer->pop(buf,1024);
        MESSAGE_TYPE*m = (MESSAGE_TYPE*) buf;

        if(*m == DATA_MSG) {
            chan->cwrite(buf,sizeof(datamsg));
            chan->cread(&resp, sizeof(double));
            hc->update(((datamsg*)buf)->person,resp);
        }

        else if(*m ==QUIT_MSG) {
            chan->cwrite(m, sizeof(MESSAGE_TYPE));
            delete chan;
            break;

        }
        else if(*m ==FILE_MSG) {
            filemsg* fm = (filemsg*) buf;
            string fname = (char*) (fm+1);
            int sz = sizeof(filemsg)+ fname.size()+1;
            chan->cwrite(buf, sz);
            chan->cread(recvbuf, mb);

            string recvfname = "recv/" + fname;

            FILE* fp = fopen(recvfname.c_str(),"r+");
            fseek(fp,fm->offset , SEEK_SET);
            fwrite(recvbuf,1,fm->length, fp);
            fclose(fp);
        }
    }
}

void file_thread_function(string fname, BoundedBuffer* request_buffer, TCPRequestChannel* chan, int mb) {
    char buf[1024];
    filemsg f(0, 0);
    memcpy(buf, &f, sizeof(f));
    strcpy(buf + sizeof(f), fname.c_str());
    chan->cwrite(buf, sizeof(f) + fname.size() + 1);
    __int64_t filelength;
    chan->cread(&filelength, sizeof(filelength));

    string recvfname = "recv/" + fname;
    FILE* fp = fopen(recvfname.c_str(), "w");
    fseek(fp, filelength, SEEK_SET);
    fclose(fp);

    filemsg* fm = (filemsg*) buf;
    __int64_t remlen = filelength;
    while(remlen > 0) {
        fm->length = min(remlen, (__int64_t)mb);
        request_buffer->push(buf, sizeof(f) + fname.size() + 1);
        fm->offset += fm->length;
        remlen -= fm->length;
    }
}

struct Response {
    int person;
    double ecg;
};

void histogram_thread_function (BoundedBuffer* response_buffer, HistogramCollection* hc){
    /*
		Functionality of the histogram threads	
    */
   char buf[1024];
   while(true) {
       response_buffer->pop(buf, 1024);
       Response* r = (Response*) buf;
       if (r->person == -1) {
           break;
       }
       hc->update(r->person, r->ecg);
   }
}

void event_polling_thread(int w, int mb, TCPRequestChannel** wchans, BoundedBuffer* request_buffer, HistogramCollection* hc)
{
    char buf[1024];
    double resp = 0;
    char recvbuf[mb];

    struct epoll_event ev;
    struct epoll_event events[w];

    int epollfd = epoll_create1(0);
    if (epollfd == -1) {
        EXITONERROR("epoll_create1");
    }

    unordered_map<int, int> fd_to_index; 
    vector<vector<char>> state(w);
    int nsent = 0, nrecv = 0;
    for(int i = 0; i < w; i++) {
        int sz = request_buffer->pop(buf,1024);
        wchans[i]->cwrite(buf, sz);
        state[i] = vector<char> (buf, buf+sz);
        nsent++;
        int rfd = wchans[i]->getfd();
    
        fcntl(rfd, F_SETFL, O_NONBLOCK);

        ev.events = EPOLLIN | EPOLLET;
        ev.data.fd = rfd;
        fd_to_index[rfd] = i;
        if (epoll_ctl(epollfd, EPOLL_CTL_ADD, rfd, &ev) == -1) {
            EXITONERROR("epoll_ctl: listen_sock");
        }
    }

  
    bool quit_recv = false;

    while (true) {
        if (quit_recv && nrecv == nsent) {
            break;
        }
            
        int nfds = epoll_wait(epollfd, events, w, -1);
        if (nfds == -1) {
            EXITONERROR("epoll_wait");
        }
        for(int i=0; i < nfds; i++) {
            int rfd = events[i].data.fd;
            int index = fd_to_index[rfd];
            int resp_sz = wchans[index]->cread(recvbuf, mb);
            nrecv++;

            vector<char> req = state[index];
            char* request = req.data();

            MESSAGE_TYPE* m = (MESSAGE_TYPE*) request;
            if (*m == DATA_MSG) {
                hc->update(((datamsg*) request)->person, *(double*)recvbuf);
            }
            else if (*m == FILE_MSG) {
                filemsg* fm = (filemsg*) request;
                string fname = (char*) (fm+1);
                int sz = sizeof(filemsg)+ fname.size()+1;

                string recvfname = "recv/" + fname;

                FILE* fp = fopen(recvfname.c_str(),"r+"); 
                fseek(fp,fm->offset , SEEK_SET);
                fwrite(recvbuf,1,fm->length, fp);
                fclose(fp);
            }

            if (quit_recv) {
                continue;
            }
                
            int req_sz = request_buffer->pop(buf, sizeof(buf));
            if (*(MESSAGE_TYPE* ) buf == QUIT_MSG) {
                quit_recv = true;
            }
            else {
                wchans[index]->cwrite(buf, req_sz);
                state[index] = vector<char> (buf, buf+req_sz);
                nsent++;
            }

        }
    }
}


int main(int argc, char *argv[])
{
    int n = 15000;
    int p = 15; 
    int w = 200;
    int b = 500;
    int h = 3;
	int m = MAX_MESSAGE;
    srand(time_t(NULL));
    string fname = "";
    bool filetransfer = false;

    string host_name;
    string port_no;

    int opt = -1;
    while((opt = getopt(argc,argv,"m:n:b:w:p:f:h:r:"))!=-1)
    {
        switch (opt)
        {
            case 'm':
                m = atoi(optarg);
                break;
            case 'n':
                n = atoi(optarg);
                break;
            case 'p':
                p = atoi(optarg);
                break;
            case 'b':
                b = atoi(optarg);
                break;
            case 'w':
                w = atoi(optarg);
                if (w > n * p) {
                    w = n * p;
                }
                break;
            case 'f':
                fname = optarg;
                filetransfer = true;
                break;
            case 'h':
                host_name = optarg;
                break;
            case 'r':
                port_no = optarg;
                break;            
        }
    }
    
    
    TCPRequestChannel* chan = new TCPRequestChannel(host_name, port_no);
    BoundedBuffer request_buffer(b);
    BoundedBuffer response_buffer(b);
	HistogramCollection hc;

	for(int i = 0; i < p; i++) {
	    Histogram* h = new Histogram(10,-2.0,2.0);
	    hc.add(h);
    }

	TCPRequestChannel** wchans  = new TCPRequestChannel* [w];
	for(int i = 0; i < w;i++) {
	    wchans[i] = new TCPRequestChannel(host_name, port_no);
    }
	
    struct timeval start, end;
    gettimeofday (&start, 0);

    /* Start all threads here */
    thread patient [p];
    thread filethread;
    if(filetransfer) {
        filethread = thread(file_thread_function, fname, &request_buffer, chan, m);
    }
    else {
        for(int i = 0; i < p;i++) {
            patient[i] = thread(patient_thread_function,n,i+1, &request_buffer );
        }
    }


    // thread workers[w];
    // for(int i = 0; i < w;i++) {
    //     workers[i] = thread(worker_thread_function, wchans[i],&request_buffer, &hc,m);
    // }

    thread evp (event_polling_thread, w, m, wchans, &request_buffer, &hc);

    // thread hists[h];
    // for (int i = 0; i < h; i++) {
    //     hists[i] = thread(histogram_thread_function, &response_buffer, &hc);
    // }
	

	/* Join all threads here */
	if(filetransfer) {
        filethread.join();
    }
    else {
        for(int i = 0; i < p;i++) {
            patient[i].join();
        }
    }

    cout << "Patient threads finished" <<endl;

    // for(int i = 0; i < w;i++)  {
    //     MESSAGE_TYPE q = QUIT_MSG;
    //     request_buffer.push ((char*) &q,sizeof(q));
    // }

    // for(int i = 0; i < w;i++) {
    //     workers[i].join();
    // }

    MESSAGE_TYPE q = QUIT_MSG;
    request_buffer.push((char*) &q, sizeof(q));
    evp.join();
    cout << "Worker threads finished" <<endl;

    // for(int i = 0; i < h;i++)  {
    //     MESSAGE_TYPE q = QUIT_MSG;
    //     request_buffer.push ((char*) &q,sizeof(q));
    // }

    // for (int i = 0; i < h; i++) {
    //     hists[i].join();
    // }

    gettimeofday (&end, 0);

    // print the results
	hc.print ();

    timediff(start, end);
    cout << "All Done!!!" << endl;

    chan->cwrite(&q, sizeof(MESSAGE_TYPE));
    delete chan;
    for(int i = 0; i < w; i++) {
        wchans[i]->cwrite(&q, sizeof(MESSAGE_TYPE));
        delete wchans[i];
    }
    delete[] wchans;

    wait(0);
}