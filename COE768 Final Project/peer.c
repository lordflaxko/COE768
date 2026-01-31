#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/stat.h>
#include <errno.h>

#define BUFSIZE 100        /* buffer size for file chunks */
#define MAX_LOCAL 5        /* max locally stored content names */
#define NAME_SIZE 64       /* max size for names */

struct downloadsend {
    char type;             /* C = chunk, F = final, E = error */
    char data[BUFSIZE];    /* file data chunk */
};

struct pdu {
    char type;             /* request type: R, T, O, S, D */
    char user[NAME_SIZE];  /* username */
    char contentName[NAME_SIZE]; /* content file name */
    struct sockaddr_in addr;     /* client TCP address */
};

/* prototypes */
void printoption();
void userInit(struct sockaddr_in reg_addr, int s, char *user, int *registered);
void clientInit(int sd);
int initPDU(struct pdu *senderf, char type, char user[], char contentName[]);
int serverconnection_1(int s, char contname[], struct sockaddr_in addr, char user[]);

/* local registry of contents served by this peer */
char localContents[MAX_LOCAL][NAME_SIZE];
int localCount = 0;

int main(int argc, char **argv)
{
    char *host = "localhost";  /* default index server */
    int port = 3000;           /* default UDP port */

    struct hostent *phe;
    struct sockaddr_in C_client;
    struct sockaddr_in C_address;
    struct sockaddr_in C_server;

    int s;                     /* UDP socket to index server */
    int sd, tempsd, selector;  /* TCP server/listener */

    char user[NAME_SIZE];
    user[0] = '\0';            /* user not set yet */

    int registered = 0;        /* count of user's registered content */
    int *regPtr = &registered;

    int templen;
    fd_set rfds, afds;

    /* parse command arguments */
    switch (argc) {
    case 1:
        break;
    case 2:
        host = argv[1];
        break;
    case 3:
        host = argv[1];
        port = atoi(argv[2]);
        break;
    default:
        fprintf(stderr, "usage: peer [host [port]]\n");
        exit(1);
    }

    /* setup server address */
    memset(&C_server, 0, sizeof(C_server));
    C_server.sin_family = AF_INET;
    C_server.sin_port = htons(port);

    if ((phe = gethostbyname(host))) {
        memcpy(&C_server.sin_addr, phe->h_addr, phe->h_length);
    } else if ((C_server.sin_addr.s_addr = inet_addr(host)) == INADDR_NONE) {
        fprintf(stderr,"Can't get host entry\n");
    }

    /* create UDP socket */
    s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) {
        perror("socket UDP");
        exit(1);
    }

    /* connect UDP socket to index server */
    if (connect(s, (struct sockaddr *)&C_server, sizeof(C_server)) < 0) {
        perror("connect UDP");
        exit(1);
    }

    /* create TCP listening socket for sending files */
    if ((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        fprintf(stderr, "Can't create TCP socket\n");
        exit(1);
    }

    /* bind to ephemeral port */
    bzero((char*)&C_address, sizeof(struct sockaddr_in));
    C_address.sin_family = AF_INET;
    C_address.sin_port = htons(0);
    C_address.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (bind(sd, (struct sockaddr*)&C_address, sizeof(C_address)) == -1) {
        fprintf(stderr, "Can't bind name to socket\n");
        exit(1);
    }

    templen = sizeof(struct sockaddr_in);
    getsockname(sd,(struct sockaddr*)&C_address,&templen);

    /* begin listening for download requests */
    if (listen(sd, 5) < 0) {
        fprintf(stderr, "Listening failed\n");
        exit(1);
    }

    printf("Content server listening on 127.0.0.1:%d\n", ntohs(C_address.sin_port));
    printf("Enter username:\n");

    /* main event loop */
    while (1) {
        FD_ZERO(&afds);
        FD_SET(0, &afds);     /* keyboard input */
        FD_SET(sd, &afds);    /* TCP incoming */

        memcpy(&rfds, &afds, sizeof(rfds));

        if ((selector = select(FD_SETSIZE, &rfds, NULL, NULL, NULL)) < 0) {
            perror("select");
            exit(1);
        }

        /* handle incoming TCP connection */
        if (FD_ISSET(sd, &rfds)) {
            int tempsd_len = sizeof(C_client);
            tempsd = accept(sd, (struct sockaddr*)&C_client, &tempsd_len);
            if (tempsd >= 0) {
                clientInit(tempsd); /* send file */
                close(tempsd);
                printf("Enter Command:\n");
            }
        }

        /* handle user input */
        if (FD_ISSET(fileno(stdin), &rfds)) {
            userInit(C_address, s, user, regPtr);
            printf("Enter Command:\n");
        }
    }

    close(sd);
    exit(EXIT_SUCCESS);
}

/* process user commands from terminal */
void userInit(struct sockaddr_in C_address, int s, char *user, int *registered)
{
    struct pdu senderf;
    struct downloadsend rpdu;
    struct pdu temps1;

    char cmd;
    char contentName[NAME_SIZE];
    int getserver;
    char contname[NAME_SIZE];

    /* initialize buffers */
    contentName[0] = '\0';
    contname[0] = '\0';

    /* first input must be username */
    if (user[0] == '\0') {
        scanf("%63s", user);
        user[NAME_SIZE - 1] = '\0';
        printf("Enter Command:\n");
    }

    scanf(" %c", &cmd); /* read next command */

    switch (cmd) {
    case '?':
        printoption();
        break;

    case 'Q':  /* quit and deregister all */
    {
        if (*registered > 0) {
            int i;
            printf("Deregistering all %d registered contents before quit...\n",
                   *registered);

            /* notify index server for each registered content */
            for (i = 0; i < localCount; i++) {
                initPDU(&senderf, 'T', user, localContents[i]);
                if (write(s, &senderf, sizeof(senderf)) < 0) {
                    perror("write deregister");
                    break;
                }
                if (recv(s, &rpdu, sizeof(rpdu), 0) <= 0) {
                    perror("recv deregister");
                    break;
                }
            }
        }
        printf("Bye.\n");
        exit(0);
    }

    case 'D':  /* download content */
    {
        printf("Which content do you want to download?\n");
        scanf("%63s", contname);
        contname[NAME_SIZE - 1] = '\0';

        getserver = serverconnection_1(s, contname, C_address, user);
        if (getserver < 0) {
            printf("No such content available\n\n");
            break;
        }

        int n_2;
        int receivefiles = 1;
        int receiveconnections;
        struct downloadsend C_download;
        FILE *f1;

        temps1.type = 'D';
        strncpy(temps1.contentName, contname, NAME_SIZE - 1);
        temps1.contentName[NAME_SIZE - 1] = '\0';

        /* send download request to remote peer */
        n_2 = write(getserver, &temps1, sizeof(temps1));
        if (n_2 < 0) {
            perror("write download request");
            close(getserver);
            break;
        }

        f1 = fopen(contname,"w");
        if (f1 == NULL) {
            perror("fopen");
            receivefiles = 1;
        } else {
            /* receive file chunks */
            while (1) {
                receiveconnections = recv(getserver, &C_download, sizeof(C_download), 0);
                if (receiveconnections <= 0) {
                    receivefiles = 1;
                    break;
                }

                C_download.data[BUFSIZE-1] = '\0';

                if (C_download.type == 'E') {
                    printf("ERROR IN DOWNLOAD\n");
                    remove(contname);
                    receivefiles = 1;
                    break;
                } else {
                    fprintf(f1, "%s", C_download.data);

                    if (C_download.type == 'F') {
                        receivefiles = 100; /* done */
                        break;
                    }
                }
            }
            fclose(f1);
        }

        close(getserver);

        if (receivefiles == 1)
            break;

        /* auto-register downloaded content */
        cmd = 'R';
        strncpy(contentName, contname, NAME_SIZE - 1);
        contentName[NAME_SIZE - 1] = '\0';
        /* fall-through */
    }

    case 'R':  /* register content */
    {
        if (cmd == 'R' && contentName[0] == '\0') {
            printf("What content do you want to register?\n");
            scanf("%63s", contentName);
            contentName[NAME_SIZE - 1] = '\0';
        }

        initPDU(&senderf, 'R', user, contentName);
        senderf.addr = C_address;

        printf("Port number: %d\n", ntohs(senderf.addr.sin_port));

        if (write(s, &senderf, sizeof(senderf)) < 0) {
            perror("write register");
            break;
        }

        if (recv(s, &rpdu, sizeof(rpdu), 0) <= 0) {
            perror("recv register");
            break;
        }

        if (rpdu.type == 'E') {
            printf("%s\n", rpdu.data);
        } else if (rpdu.type == 'A') {
            printf("\nContent Name: %s", senderf.contentName);
            printf("\nIn client Port: '%d'\n\n", ntohs(senderf.addr.sin_port));

            /* store locally */
            if (localCount < MAX_LOCAL) {
                strncpy(localContents[localCount], senderf.contentName, NAME_SIZE - 1);
                localContents[localCount][NAME_SIZE - 1] = '\0';
                localCount++;
            }
            (*registered)++;
        }
        break;
    }

    case 'T':  /* deregister one content */
    {
        if (*registered == 0) {
            printf("No content to deregister.\n");
        } else {
            printf("What content do you want to deregister?\n");
            scanf("%63s", contentName);
            contentName[NAME_SIZE - 1] = '\0';

            initPDU(&senderf, 'T', user, contentName);

            if (write(s, &senderf, sizeof(senderf)) < 0) {
                perror("write deregister");
                break;
            }
            if (recv(s, &rpdu, sizeof(rpdu), 0) <= 0) {
                perror("recv deregister");
                break;
            }

            if (rpdu.type == 'A') {
                /* remove from local list */
                int i, j;
                for (i = 0; i < localCount; i++) {
                    if (strcmp(localContents[i], contentName) == 0) {
                        for (j = i; j < localCount-1; j++)
                            strcpy(localContents[j], localContents[j+1]);
                        bzero(localContents[localCount-1],
                              sizeof(localContents[localCount-1]));
                        localCount--;
                        break;
                    }
                }
                --(*registered);
                printf("Deregistered content '%s'.\n", contentName);
            }
        }
        break;
    }

    case 'O': /* list all registered content */
        senderf.type = 'O';
        if (write(s, &senderf, sizeof(senderf)) < 0) {
            perror("write list");
            break;
        }
        if (recv(s, &rpdu, sizeof(rpdu), 0) <= 0) {
            perror("recv list");
            break;
        }
        printf("Online Content:\n%s\n", rpdu.data);
        break;

    default:
        printf("Unknown command. Type ? for options.\n");
        break;
    }
}

/* handle incoming TCP download request */
void clientInit(int sd)
{
    struct pdu rpdu;
    struct downloadsend senderf;

    char fileName[NAME_SIZE];
    char fileNotFound[] = "FILE NOT FOUND\n";

    int n;
    FILE *file;

    /* receive request PDU */
    if ((n = recv(sd, &rpdu, sizeof(rpdu), 0)) == -1) {
        fprintf(stderr, "Content Server recv: %s (%d)\n",
                strerror(errno), errno);
        exit(EXIT_FAILURE);
    }

    /* handle download request */
    if (rpdu.type == 'D') {
        strncpy(fileName, rpdu.contentName, NAME_SIZE - 1);
        fileName[NAME_SIZE - 1] = '\0';

        char filePath[NAME_SIZE + 4];
        snprintf(filePath, sizeof(filePath), "./%s", fileName);

        file = fopen(filePath, "r");
        if (file == NULL) {
            senderf.type = 'E';
            memcpy(senderf.data, fileNotFound, sizeof(fileNotFound));
            write(sd, &senderf, sizeof(senderf));
        } else {
            printf("Sending file...\n");

            struct stat filesizenum;
            struct downloadsend packet;
            char filename[BUFSIZE] = {0};

            int nfread, sendingsdata, totaldata = 0;

            stat(fileName, &filesizenum);

            /* send data until EOF */
            while ((nfread = fread(filename, 1, BUFSIZE, file)) > 0) {
                packet.type = (totaldata + nfread >= filesizenum.st_size)
                              ? 'F' : 'C';

                memcpy(packet.data, filename, nfread);
                packet.data[nfread < BUFSIZE ? nfread : BUFSIZE - 1] = '\0';

                if ((sendingsdata = send(sd, &packet, sizeof(packet), 0)) == -1) {
                    printf("Error in send\n");
                    exit(1);
                } else {
                    totaldata += nfread;
                    bzero(filename, BUFSIZE);
                }
            }

            printf("Successfully sent file\n\n");
            fclose(file);
        }
    }
}

/* contact index server to locate who has a file */
int serverconnection_1(int s, char contname[], struct sockaddr_in addr, char user[])
{
    struct pdu senderf, rpdu;

    int tcp_sock;
    int n;

    memset(&senderf, 0, sizeof(senderf));
    memset(&rpdu, 0, sizeof(rpdu));

    initPDU(&senderf, 'S', user, contname);
    senderf.addr = addr;

    /* send search request */
    if (write(s, &senderf, sizeof(senderf)) < 0) {
        perror("write search");
        return -1;
    }

    /* receive response */
    n = recv(s, &rpdu, sizeof(rpdu), 0);
    if (n <= 0) {
        perror("recv search");
        return -1;
    }

    if (rpdu.type == 'E') {
        return -1;
    } else if (rpdu.type == 'S') {
        printf("Connecting to content server using address.\n");
        printf("Using Port: '%d'\n", ntohs(rpdu.addr.sin_port));
    } else {
        printf("Unexpected response type '%c' from server\n", rpdu.type);
        return -1;
    }

    /* create TCP connection to peer */
    if ((tcp_sock = socket(AF_INET, SOCK_STREAM,0)) == -1) {
        fprintf(stderr, "Cannot make the socket.\n");
        exit(EXIT_FAILURE);
    }

    if (connect(tcp_sock, (struct sockaddr *)&rpdu.addr,
                sizeof(rpdu.addr)) == -1) {
        fprintf(stderr, "No connection available %s (%d)\n",
                strerror(errno), errno);
        close(tcp_sock);
        return -1;
    }

    return tcp_sock;
}

/* show available commands */
void printoption()
{
    printf("\n");
    printf("? = Show option\n");
    printf("R = Register content\n");
    printf("T = Unregister content\n");
    printf("O = Show Online content\n");
    printf("D = Download content\n");
    printf("Q = Quit (and deregister all)\n\n");
}

/* fill PDU structure */
int initPDU(struct pdu *senderf, char type, char user[], char contentName[])
{
    senderf->type = type;
    strncpy(senderf->user, user, NAME_SIZE - 1);
    senderf->user[NAME_SIZE - 1] = '\0';
    strncpy(senderf->contentName, contentName, NAME_SIZE - 1);
    senderf->contentName[NAME_SIZE - 1] = '\0';
    return 0;
}
