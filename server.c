/*
** server.c -- a stream socket server demo
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <pulse/simple.h>
#include <pulse/error.h>
#include <pulse/gccmacro.h>
#include "codec.c"
//#define PORT "3490"  // the port users will be connecting to

#define BACKLOG 10     // how many pending connections queue will hold
#define BUFSIZE 1024
#define INTERVAL 2        /* number of milliseconds to go off */

int sockfd, new_fd;
uint8_t buf[BUFSIZE];
pa_simple *s;
int error;
FILE *in;

void sigchld_handler(int s)
    {
    // waitpid() might overwrite errno, so we save and restore it:
    int saved_errno = errno;

    /*The waitpid() system call suspends execution of the calling process until a child specified by pid argument has changed  
    state. By default, waitpid() waits only for terminated children. -1 means wait for any child process. 
    WNOHANG means return immediately if no child has exited. See man page for more details.*/
    while(waitpid(-1, NULL, WNOHANG) > 0);
    errno = saved_errno;
    }


void stream_read();

//Signal Handler
void my_handler_for_sigint(int signumber)
    {
    if (signumber==SIGALRM)
        {
        stream_read();
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

static ssize_t loop_write(int fd, const void*data, size_t size) 
    {
    ssize_t ret = 0;

    while (size > 0) 
        {
        ssize_t r;

        if ((r = write(fd, data, size)) < 0)
            return r;

        if (r == 0)
            break;

        ret += r;
        data = (const uint8_t*) data + r;
        size -= (size_t) r;
        }

    return ret;
    }

    
void stream_read()
    {    
    //uint8_t buf[BUFSIZE];
    ssize_t r;
    int i;
    /* Read some data ... */
    if ((r = read(new_fd, buf, sizeof(buf))) <= 0) 
        {
        if (r == 0) /* EOF */
            return;
        fprintf(stderr, __FILE__": read() failed: %s\n", strerror(errno));
        if (s)
            pa_simple_free(s);
        }

    
    /* decoding using g711 */
    for(i=0; i<BUFSIZE; i++)
        {   
        buf[i] = (uint8_t)ulaw2linear((int)buf[i]);
        }

    /* ... and play it */
    if (pa_simple_write(s, buf, (size_t) r, &error) < 0) 
        {
        fprintf(stderr, __FILE__": pa_simple_write() failed: %s\n", pa_strerror(error));
        if (s)
            pa_simple_free(s);
        }

    if (loop_write(fileno(in), buf, sizeof(buf)) != sizeof(buf))
        {
        fprintf(stderr, __FILE__": write() failed: %s\n", strerror(errno));
        //goto finish;
        }

    if(strcmp(buf, "Ending Connection.")==0)
        {
        close(new_fd);
        exit(0);
        }
    }

int main(int argc, char *argv[])
    {
    //int sockfd, new_fd;  // listen on sock_fd, new connection on new_fd : both are file descriptors
    int numbytes;
    //char buf[MAXDATASIZE];
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_storage their_addr; // connector's address information
    socklen_t sin_size;
    struct sigaction sa;
    int yes=1;
    char s_array[INET6_ADDRSTRLEN];
    int rv;

    int ret = 1;

    in = fopen("output.txt", "w+");

    struct itimerval it_val;    /* for setting itimer */

  /* Upon SIGALRM, call stream_read().
   * Set interval timer.  We want frequency in ms, 
   * but the setitimer call needs seconds and useconds. */
    it_val.it_value.tv_sec =     INTERVAL/1000;
    it_val.it_value.tv_usec =    (INTERVAL*1000) % 1000000;   
    it_val.it_interval = it_val.it_value;

    if (signal(SIGALRM, my_handler_for_sigint) == SIG_ERR)
      printf("\ncan't catch SIGALRM\n");
    

    ////////////////////////////////////Binding to socket and accepting connections///////////////////

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // use my IP
    if (argc != 2) 
        {
        fprintf(stderr,"usage: server port_number\n");
        exit(1);
        }
    if ((rv = getaddrinfo(NULL, argv[1], &hints, &servinfo)) != 0) 
        {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
        }

    // loop through all the results and bind to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) 
        {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) 
            {
            perror("server: socket");
            continue;
            }

        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) 
            {
            perror("setsockopt");
            exit(1);
            }

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) 
            {
            close(sockfd);
            perror("server: bind");
            continue;
            }

        break;
        }

    freeaddrinfo(servinfo); // all done with this structure

    if (p == NULL)  
        {
        fprintf(stderr, "server: failed to bind\n");
        exit(1);
        }

    if (listen(sockfd, BACKLOG) == -1) 
        {
        perror("listen");
        exit(1);
        }

    sa.sa_handler = sigchld_handler; // reap all dead processes
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) 
        {
        perror("sigaction");
        exit(1);
        }

    printf("server: waiting for connections...\n");

    while(1) 
        {  // main accept() loop. The call to accept() is run in an infinite loop so that the server is always running
        sin_size = sizeof their_addr;
        new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
        if (new_fd == -1) 
            {
            perror("accept");
            continue;
            }

        inet_ntop(their_addr.ss_family,
            get_in_addr((struct sockaddr *)&their_addr),
            s_array, sizeof s_array);
        printf("server: got connection from %s\n", s_array);

/////////////////////////////////////Receiving Audio Inputs from the Client and playing it////////////////

        if (!fork()) // this is the child process; created for each client
            { 
            close(sockfd); // child doesn't need the listener

            static const pa_sample_spec ss = 
                {
                .format = PA_SAMPLE_S16LE, //the sample format: eg. PA_SAMPLE_S16LE: Signed 16 Bit PCM, little endian (PC) 
                .rate = 44100, //sample rate
                .channels = 2 //audio channels, 1 for mono, 2 for stereo etc
                };

            /*pa_simple Struct Reference. An opaque simple connection object. */
            //pa_simple *s = NULL;
            //int error;
                
            /* Create a new playback stream */
            //this helps connect to the server
            if (!(s = pa_simple_new(NULL, argv[0], PA_STREAM_PLAYBACK, NULL, "playback", &ss, NULL, NULL, &error))) 
                {
                fprintf(stderr, __FILE__": pa_simple_new() failed: %s\n", pa_strerror(error));
                goto finish;
                }


            if (setitimer(ITIMER_REAL, &it_val, NULL) == -1) 
                {
                perror("error calling setitimer()");
                exit(1);
                }

    while (1) 
        pause();
            // while(1)
            //     {    
            //     uint8_t buf[BUFSIZE];
            //     ssize_t r;

            //     /* Read some data ... */
            //     if ((r = read(new_fd, buf, sizeof(buf))) <= 0) 
            //         {
            //         if (r == 0) /* EOF */
            //             break;
            //         fprintf(stderr, __FILE__": read() failed: %s\n", strerror(errno));
            //         goto finish;
            //         }

            //     /* ... and play it */
            //     if (pa_simple_write(s, buf, (size_t) r, &error) < 0) 
            //         {
            //         fprintf(stderr, __FILE__": pa_simple_write() failed: %s\n", pa_strerror(error));
            //         goto finish;
            //         }

            //     if (loop_write(fileno(in), buf, sizeof(buf)) != sizeof(buf))
            //         {
            //         fprintf(stderr, __FILE__": write() failed: %s\n", strerror(errno));
            //         //goto finish;
            //         }

            //     if(strcmp(buf, "Ending Connection.")==0)
            //         {
            //         close(new_fd);
            //         exit(0);
            //         }
            //     }
            finish:

            if (s)
                pa_simple_free(s);

            }
        close(new_fd);  // parent doesn't need this
        }

    ret = 0;
    return ret;
    
    //return 0;
    }