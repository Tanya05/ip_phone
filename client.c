/*
** client.c -- a stream socket client demo to send audio over a socket
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <signal.h>
#include <arpa/inet.h>
#include <pulse/simple.h>
#include <pulse/error.h>
#include <pulse/gccmacro.h>
#include "codec.c"

//#define PORT "3490" //can hardcode the port client will be connecting to, here accepts from user so commented out 

//#define MAXDATASIZE 100 // max number of bytes we can get at once 
#define BUFSIZE 1024
#define INTERVAL 2        /* number of milliseconds to go off */

uint8_t buf[BUFSIZE];
int sockfd; 
FILE *in; 
pa_simple *s;
int clientSocket; //for signal handling
int error;

void stream_read();

//Signal Handler
void my_handler_for_sigint(int signumber)
    {
    char ans[2];
    if (signumber == SIGINT)
        {
        printf("received SIGINT\n");
        printf("Program received a CTRL-C\n");
        printf("Terminate Y/N : "); 
        scanf("%s", ans);
        if ((strcmp(ans,"Y") == 0)||(strcmp(ans,"y") == 0))
            {
            printf("Exiting ....\n");
            if (send(clientSocket, "Ending Connection.", BUFSIZE-1, 0) == -1)//will send end of connection string in the end
                {
                close(clientSocket);
                perror("send");
                }
            close(clientSocket);
            exit(0); 
            }
        else
            printf("Continung ..\n");
        }
    else if (signumber==SIGALRM)
        {
        stream_read();
        }
    }

/* A simple routine calling UNIX write() and send() in a loop */
static ssize_t loop_write(int fd, int sockfd, const void*data, size_t size) 
    {
    ssize_t ret = 0; //This data type is used to represent the sizes of blocks that can be read or written in a single operation of a sifned type.

    while (size > 0) //because of this exactly the number of bytes recorded is written 
        {
        ssize_t r; 

        if (r= send(sockfd, data, size,0) < 0) //write returns no of bytes written
            return r;

        if ((r = write(fd, data, size)) < 0) 
            return r;

        if (r == 0)
            break; //go to next iteration as the following steps are for nothing if r = 0 

        ret += r; //ret keeping a track of all bytes wriiten
        data = (const uint8_t*) data + r; //uint8_t is an unsigned 8 bit integer.
        size -= (size_t) r; //size being reduced by number of bytes written in one iteration
        }

    return ret; //ret finally is thus the total number of bytes written
    }

void stream_read()
    {
    int i;
    if (pa_simple_read(s, buf, sizeof(buf), &error) < 0) //this records from microphone
        {
        fprintf(stderr, __FILE__": pa_simple_read() failed: %s\n", pa_strerror(error));
        //goto finish;
        }

    /* encoding using g711 */
    for(i=0; i<BUFSIZE; i++)
        {   
        buf[i] = (uint8_t)linear2ulaw((int)buf[i]);
        }

    if (loop_write(fileno(in), sockfd, buf, sizeof(buf)) != sizeof(buf)) 
        {
        fprintf(stderr, __FILE__": write() failed: %s\n", strerror(errno));
        close(sockfd);
        perror("send");
        //goto finish;
        }
    }

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
    {
    if (sa->sa_family == AF_INET) 
        {
        return &(((struct sockaddr_in*)sa)->sin_addr);
        }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
    }

int main(int argc, char *argv[])
    {
    //int sockfd;  
    struct addrinfo hints, *servinfo, *p;
    int rv;
    char s_array[INET6_ADDRSTRLEN];
    in = fopen("input.txt", "w+");

    struct itimerval it_val;    /* for setting itimer */

  /* Upon SIGALRM, call stream_read().
   * Set interval timer.  We want frequency in ms, 
   * but the setitimer call needs seconds and useconds. */
    it_val.it_value.tv_sec =     INTERVAL/1000;
    it_val.it_value.tv_usec =    (INTERVAL*1000) % 1000000;   
    it_val.it_interval = it_val.it_value;

    //Signal handler
    if (signal(SIGINT, my_handler_for_sigint) == SIG_ERR)
      printf("\ncan't catch SIGINT\n");
    if (signal(SIGALRM, my_handler_for_sigint) == SIG_ERR)
      printf("\ncan't catch SIGALRM\n");

    if (argc != 3) 
        {
        fprintf(stderr,"usage: client hostname port_number\n");
        exit(1);
        }

/////////////////////////////// create stream////////////////////////////////////////////////////
    static const pa_sample_spec ss = 
        {
        .format = PA_SAMPLE_S16LE,
        .rate = 44100,
        .channels = 2
        };
    //pa_simple *s = NULL;
    int ret = 1;
    int error;

    /* Create the recording stream 
    s = pa_simple_new(NULL,            // Use the default server.
                  argv[0],             // Our application's name.
                  PA_STREAM_RECORD,    // Record stream. 
                  NULL,                // Use the default device.
                  "record",            // Description of our stream.
                  &ss,                 // Our sample format.
                  NULL,                // Use default channel map
                  NULL,                // Use default buffering attributes.
                  &error,              // for error handling.
                  );*/

    //this helps connect to the server
    if (!(s = pa_simple_new(NULL, argv[0], PA_STREAM_RECORD, NULL, "record", &ss, NULL, NULL, &error))) {
        fprintf(stderr, __FILE__": pa_simple_new() failed: %s\n", pa_strerror(error));
        goto finish;
    }

////////////////////////////// connect to socket//////////////////////////////////////////////////////

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if ((rv = getaddrinfo(argv[1], argv[2], &hints, &servinfo)) != 0) 
        {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
        }

    // loop through all the results and connect to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) 
        {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) 
            {
            perror("client: socket");
            continue;
            }

        if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) 
            {
            close(sockfd);
            perror("client: connect");
            continue;
            }
        break;
        }

    if (p == NULL) 
        {
        fprintf(stderr, "client: failed to connect\n");
        return 2;
        }

    inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr),
            s_array, sizeof s_array);
    printf("client: connecting to %s\n", s_array);

    freeaddrinfo(servinfo); // all done with this structure
    clientSocket = sockfd; //assigning to global variable for signal handling signal handling

    
////////////////////////////// record and send to server over socket////////////////////////////////////

    //loop for recording data and send over socket
    
    if (setitimer(ITIMER_REAL, &it_val, NULL) == -1) 
        {
        perror("error calling setitimer()");
        exit(1);
        }

    while (1) 
        pause();

    // for (;;) 
    //     {
    //     //uint8_t buf[BUFSIZE];

    //     /* Record some data ... */
    //     if (pa_simple_read(s, buf, sizeof(buf), &error) < 0) //this records from microphone
    //         {
    //         fprintf(stderr, __FILE__": pa_simple_read() failed: %s\n", pa_strerror(error));
    //         goto finish;
    //         }

    //     if (loop_write(fileno(in), sockfd, buf, sizeof(buf)) != sizeof(buf)) 
    //         {
    //         fprintf(stderr, __FILE__": write() failed: %s\n", strerror(errno));
    //         close(sockfd);
    //         perror("send");
    //         goto finish;
    //         }
    //     }

    ret = 0;

    close(sockfd);
    finish:

    if (s)
        pa_simple_free(s);

    return ret;

    //return 0;
    }