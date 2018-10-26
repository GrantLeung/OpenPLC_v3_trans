//-----------------------------------------------------------------------------
// Copyright 2015 Thiago Alves
// This file is part of the OpenPLC Software Stack.
//
// OpenPLC is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// OpenPLC is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with OpenPLC.  If not, see <http://www.gnu.org/licenses/>.
//------
//
// This is the file for the network routines of the OpenPLC. It has procedures
// to create a socket, bind it and start network communication.
// Thiago Alves, Dec 2015
//-----------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>

#include "ladder.h"

#define MAX_INPUT 16
#define MAX_OUTPUT 16
#define MAX_MODBUS 100


//-----------------------------------------------------------------------------
// Verify if all errors were cleared on a socket
//-----------------------------------------------------------------------------
int getSO_ERROR(int fd) 
{
   int err = 1;
   socklen_t len = sizeof err;
   if (-1 == getsockopt(fd, SOL_SOCKET, SO_ERROR, (char *)&err, &len))
      perror("getSO_ERROR");
   if (err)
      errno = err;              // set errno to the socket SO_ERROR
   return err;
}

//-----------------------------------------------------------------------------
// Properly close a socket
//-----------------------------------------------------------------------------
void closeSocket(int fd) 
{
   if (fd >= 0) 
   {
      getSO_ERROR(fd); // first clear any errors, which can cause close to fail
      if (shutdown(fd, SHUT_RDWR) < 0) // secondly, terminate the 'reliable' delivery
         if (errno != ENOTCONN && errno != EINVAL) // SGI causes EINVAL
            perror("shutdown");
      if (close(fd) < 0) // finally call close()
         perror("close");
   }
}

//-----------------------------------------------------------------------------
// Set or Reset the O_NONBLOCK flag from sockets
//-----------------------------------------------------------------------------
bool SetSocketBlockingEnabled(int fd, bool blocking)
{
   if (fd < 0) return false;
   int flags = fcntl(fd, F_GETFL, 0);
   if (flags == -1) return false;
   flags = blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);
   return (fcntl(fd, F_SETFL, flags) == 0) ? true : false;
}

//-----------------------------------------------------------------------------
// Create the socket and bind it. Returns the file descriptor for the socket
// created.
//-----------------------------------------------------------------------------
int createSocket(int port)
{
    unsigned char log_msg[1000];
    int socket_fd;
    struct sockaddr_in server_addr;

    //Create TCP Socket
    socket_fd = socket(AF_INET,SOCK_STREAM,0);
    if (socket_fd<0)
    {
        sprintf(log_msg, "Modbus Server: error creating stream socket => %s\n", strerror(errno));
        log(log_msg);
        return -1;
    }
    
    //Set SO_REUSEADDR
    int enable = 1;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0)
        perror("setsockopt(SO_REUSEADDR) failed");
    
    SetSocketBlockingEnabled(socket_fd, false);

    //Initialize Server Struct
    bzero((char *) &server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    //Bind socket
    if (bind(socket_fd,(struct sockaddr *)&server_addr,sizeof(server_addr)) < 0)
    {
        sprintf(log_msg, "Modbus Server: error binding socket => %s\n", strerror(errno));
        log(log_msg);
        return -1;
    }
    
    // we accept max 5 pending connections
    listen(socket_fd,5);
    sprintf(log_msg, "Modbus Server: Listening on port %d\n", port);
    log(log_msg);

    return socket_fd;
}

//-----------------------------------------------------------------------------
// Blocking call. Wait here for the client to connect. Returns the file
// descriptor to communicate with the client.
//-----------------------------------------------------------------------------
int waitForClient(int socket_fd)
{
    unsigned char log_msg[1000];
    int client_fd;
    struct sockaddr_in client_addr;
    socklen_t client_len;

    sprintf(log_msg, "Modbus Server: waiting for new client...\n");
    log(log_msg);

    client_len = sizeof(client_addr);
    while (run_modbus)
    {
        client_fd = accept(socket_fd, (struct sockaddr *)&client_addr, &client_len); //non-blocking call
        if (client_fd > 0)
        {
            SetSocketBlockingEnabled(client_fd, true);
            break;
        }
        sleepms(100);
    }

    return client_fd;
}

//-----------------------------------------------------------------------------
// Blocking call. Holds here until something is received from the client.
// Once the message is received, it is stored on the buffer and the function
// returns the number of bytes received.
//-----------------------------------------------------------------------------
int listenToClient(int client_fd, unsigned char *buffer)
{
    bzero(buffer, 1024);
    int n = read(client_fd, buffer, 1024);
    return n;
}

//-----------------------------------------------------------------------------
// Process client's request
//-----------------------------------------------------------------------------
void processMessage(unsigned char *buffer, int bufferSize, int client_fd)
{
    int messageSize = processModbusMessage(buffer, bufferSize);
    write(client_fd, buffer, messageSize);
}

//-----------------------------------------------------------------------------
// Thread to handle requests for each connected client
//-----------------------------------------------------------------------------
void *handleConnections(void *arguments)
{
    unsigned char log_msg[1000];
    int client_fd = *(int *)arguments;
    unsigned char buffer[1024];
    int messageSize;

    sprintf(log_msg, "Modbus Server: Thread created for client ID: %d\n", client_fd);
    log(log_msg);

    while(run_modbus)
    {
        //unsigned char buffer[1024];
        //int messageSize;

        messageSize = listenToClient(client_fd, buffer);
        if (messageSize <= 0 || messageSize > 1024)
        {
            // something has  gone wrong or the client has closed connection
            if (messageSize == 0)
            {
                sprintf(log_msg, "Modbus Server: client ID: %d has closed the connection\n", client_fd);
                log(log_msg);
            }
            else
            {
                sprintf(log_msg, "Modbus Server: Something is wrong with the  client ID: %d message Size : %i\n", client_fd, messageSize);
                log(log_msg);
            }
            break;
        }

        processMessage(buffer, messageSize, client_fd);
    }
    //printf("Debug: Closing client socket and calling pthread_exit in server.cpp\n");
    close(client_fd);
    sprintf(log_msg, "Terminating Modbus connections thread\r\n");
    log(log_msg);
    pthread_exit(NULL);
}

//-----------------------------------------------------------------------------
// Function to start the server. It receives the port number as argument and
// creates an infinite loop to listen and parse the messages sent by the
// clients
//-----------------------------------------------------------------------------
void startServer(int port)
{
    unsigned char log_msg[1000];
    int socket_fd, client_fd;

    socket_fd = createSocket(port);
    mapUnusedIO();
    
    while(run_modbus)
    {
        client_fd = waitForClient(socket_fd); //block until a client connects
        if (client_fd < 0)
        {
            sprintf(log_msg, "Modbus Server: Error accepting client!\n");
            log(log_msg);
        }

        else
        {
            int arguments[1];
            pthread_t thread;
            int ret = -1;
            sprintf(log_msg, "Modbus Server: Client accepted! Creating thread for the new client ID: %d...\n", client_fd);
            log(log_msg);
            arguments[0] = client_fd;
            ret = pthread_create(&thread, NULL, handleConnections, arguments);
            if (ret==0) 
            {
                pthread_detach(thread);
            }
        }
    }
    close(socket_fd);
    close(client_fd);
    sprintf(log_msg, "Terminating Modbus thread\r\n");
    log(log_msg);
}
