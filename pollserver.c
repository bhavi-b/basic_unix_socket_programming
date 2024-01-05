#include <stdio.h>
#include <stdlib.h>
#include <poll.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define PORT "9034" // the port we're listening on

void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET)
    {
        return &(((struct sockaddr_in *)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6 *)sa)->sin6_addr);
}

// return a listening socket
int get_listener_socket(void)
{
    int listener; // listening socket descriptor
    int yes = 1;  // setsockopt()
    int rv;

    struct addrinfo hints, *ai, *p;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if ((rv = getaddrinfo(NULL, PORT, &hints, &ai)) != 0)
    {
        fprintf(stderr, "selectserver: %s\n", gai_strerror(rv));
        exit(1);
    }

    for (p = ai; p != NULL; p = p->ai_next)
    {
        listener = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (listener < 0)
        {
            continue;
        }

        // lose address already in use error
        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

        if (bind(listener, p->ai_addr, p->ai_addrlen) < 0)
        {
            close(listener);
            continue;
        }

        break;
    }

    freeaddrinfo(ai); // no further need

    // did not get binding
    if (p == NULL)
        return -1;

    // listen
    if (listen(listener, 10) == -1)
        return -1;

    return listener;
}

// add a new file descriptor to the polling set
void add_to_pfds(struct pollfd *pfds[], int newfd, int *fd_count, int *fd_size)
{
    // if we dont have room, realloc
    if (*fd_count == *fd_size)
    {
        *fd_size *= 2;
        *pfds = realloc(*pfds, sizeof(**pfds) * (*fd_size));
    }

    (*pfds)[*fd_count].fd = newfd;
    (*pfds)[*fd_count].events = POLLIN; // check ready to read
    (*fd_count)++;
}

// remove an index from the set
void del_from_pfds(struct pollfd pfds[], int i, int *fd_count)
{
    pfds[i] = pfds[*fd_count - 1];
    (*fd_count)--;
}

// main
int main(void)
{
    int listener;                       // listening socket
    int newfd;                          // newly accepted connection socket
    struct sockaddr_storage remoteaddr; // client addr
    socklen_t addrlen;

    char buf[256]; // buffer for client data;

    char remoteIP[INET6_ADDRSTRLEN];

    // 5 connections initially
    int fd_count = 0;
    int fd_size = 5;
    struct pollfd *pfds = malloc(sizeof *pfds * fd_size);

    // setup listening socket
    listener = get_listener_socket();

    if (listener == -1)
    {
        fprintf(stderr, "error getting listening socket\n");
        exit(1);
    }

    pfds[0].fd = listener;
    pfds[0].events = POLLIN; // report ready to read on incoming connection

    fd_count = 1; // listener

    while (1)
    {
        int poll_count = poll(pfds, fd_count, -1);

        if (poll_count == -1)
        {
            perror("poll");
            exit(1);
        }

        // run through the existing connections looking for data to read;

        for (int i = 0; i < fd_count; i++)
        {

            // check if ready to read
            if (pfds[i].revents & POLLIN)
            {
                if (pfds[i].fd == listener)
                {
                    // listener is ready to read

                    addrlen = sizeof remoteaddr;
                    newfd = accept(listener, (struct sockaddr *)&remoteaddr, &addrlen);

                    if (newfd == -1)
                    {
                        perror("accept");
                    }
                    else
                    {
                        add_to_pfds(&pfds, newfd, &fd_count, &fd_size);

                        printf("pollserver: new connection from %s on socket %d\n", inet_ntop(remoteaddr.ss_family, get_in_addr((struct sockaddr *)&remoteaddr), remoteIP, INET6_ADDRSTRLEN), newfd);
                    }
                }
                else
                {
                    // if not listener, regular client

                    int nbytes = recv(pfds[i].fd, buf, sizeof buf, 0);

                    int sender_fd = pfds[i].fd;

                    if (nbytes <= 0)
                    {
                        // got error or connection closed
                        if (nbytes == 0)
                        {
                            printf("pollserver: socket %d hung up\n", sender_fd);
                        }
                        else
                        {
                            perror("recv");
                        }

                        close(pfds[i].fd); // close the socket

                        del_from_pfds(pfds, i, &fd_count);
                    }
                    else
                    {
                        // we got data from client

                        for (int j = 0; j < fd_count; j++)
                        {
                            // send to everyone
                            int dest_fd = pfds[j].fd;

                            // except the listener and sender
                            if (dest_fd != listener && dest_fd != sender_fd)
                            {
                                if (send(dest_fd, buf, nbytes, 0) == -1)
                                {
                                    perror("send");
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    return 0;
}