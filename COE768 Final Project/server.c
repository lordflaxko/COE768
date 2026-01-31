#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <strings.h>

#define SERVER_TCP_PORT 3000    /* default UDP port for index server */
#define BUFSIZE 100             /* buffer size for listing responses */
#define MAX_CONTENT 5           /* max number of entries the index can store */
#define NAME_SIZE 64            /* max name length */

struct pdu {
    char type;                  /* response type (A, E, O, etc.) */
    char data[BUFSIZE];         /* response body/list */
};

struct content {
    char peer_name[NAME_SIZE];      /* peer username */
    char content_name[NAME_SIZE];   /* filename registered */
    int port;                       /* TCP port of peer */
    struct sockaddr_in addr;        /* TCP address of peer */
};

struct registerPDU {
    char type;                      /* request type (R, T, S, O) */
    char peer_name[NAME_SIZE];      /* username of requester */
    char content_name[NAME_SIZE];   /* content they register/search */
    struct sockaddr_in addr;        /* peer’s TCP address */
};

void obtain_list(struct content contents[MAX_CONTENT], int length, char *msg);

int main(int argc, char **argv) {
    struct sockaddr_in fsin;     /* client address */
    int s;                       /* UDP socket */
    int alen;
    struct sockaddr_in sin;      /* server address */
    int n, i;
    int port = SERVER_TCP_PORT;  /* default port */
    bool exists = false;

    struct pdu spdu;             /* generic small reply PDU */
    struct registerPDU request;  /* incoming packet */
    struct registerPDU response; /* search response */

    struct content contents[MAX_CONTENT];   /* registry table */
    int content_count = 0;                  /* number of stored entries */

    char message[BUFSIZE];                  /* content list buffer */

    /* Handle optional custom port */
    switch (argc) {
        case 1:
            break;
        case 2:
            port = atoi(argv[1]);
            break;
        default:
            fprintf(stderr, "Usage: %s [port]\n", argv[0]);
            exit(1);
    }

    /* Clear data structures */
    memset(contents, 0, sizeof(contents));
    bzero(message, sizeof(message));

    /* Prepare server UDP address */
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = INADDR_ANY;  /* accept from any interface */
    sin.sin_port = htons(port);

    /* Create UDP socket */
    s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) {
        perror("socket");
        exit(1);
    }

    /* Bind to port */
    if (bind(s, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
        perror("bind");
        exit(1);
    }

    printf("Index server listening UDP on port %d\n", port);

    alen = sizeof(fsin);

    /* Main server loop */
    while (1) {
        bzero(&request, sizeof(request));

        /* Receive any PDU from client/peer */
        if ((n = recvfrom(s, &request, sizeof(request), 0,
                          (struct sockaddr *)&fsin, &alen)) < 0) {
            perror("recvfrom");
            continue;
        }

        /* Process request based on type */
        switch (request.type) {

        case 'R':   /* Register content */
            printf("Received REGISTER request\n");
            spdu.type = 'A';    /* default accept */
            bzero(spdu.data, sizeof(spdu.data));
            exists = false;

            /* Check if username already registered */
            for (i = 0; i < content_count; i++) {
                if (strcmp(contents[i].peer_name, request.peer_name) == 0) {
                    exists = true;
                    break;
                }
            }

            /* Errors or success */
            if (exists) {
                spdu.type = 'E';
                strncpy(spdu.data, "Peer name already exists.\n",
                        sizeof(spdu.data) - 1);
            } else if (content_count >= MAX_CONTENT) {
                spdu.type = 'E';
                strncpy(spdu.data, "Index full.\n",
                        sizeof(spdu.data) - 1);
            } else {
                /* Store registration entry */
                printf("Peer name: %s\n", request.peer_name);
                printf("Content name: %s\n", request.content_name);
                printf("Port number: %d\n", ntohs(request.addr.sin_port));

                /* Fill content table entry */
                strncpy(contents[content_count].content_name,
                        request.content_name, NAME_SIZE - 1);
                contents[content_count].content_name[NAME_SIZE - 1] = '\0';

                strncpy(contents[content_count].peer_name,
                        request.peer_name, NAME_SIZE - 1);
                contents[content_count].peer_name[NAME_SIZE - 1] = '\0';

                contents[content_count].addr = request.addr;
                contents[content_count].port = request.addr.sin_port;

                content_count++;

                strncpy(spdu.data, "Registered.\n",
                        sizeof(spdu.data) - 1);
            }

            /* Send back acknowledgement/error */
            if (sendto(s, &spdu, sizeof(spdu), 0,
                       (struct sockaddr *)&fsin, sizeof(fsin)) < 0) {
                perror("sendto");
                exit(1);
            }
            break;

        case 'O':   /* List all online content */
            printf("Received LIST request\n");
            bzero(spdu.data, sizeof(spdu.data));

            if (content_count == 0) {
                spdu.type = 'E'; /* empty table */
                strncpy(spdu.data, "No Registered Content Available.\n",
                        sizeof(spdu.data) - 1);
            } else {
                obtain_list(contents, content_count, message);
                spdu.type = 'O';
                strncpy(spdu.data, message, sizeof(spdu.data) - 1);
            }

            /* Send back content list */
            if (sendto(s, &spdu, sizeof(spdu), 0,
                       (struct sockaddr *)&fsin, sizeof(fsin)) < 0) {
                perror("sendto");
                exit(1);
            }
            break;

        case 'T':   /* Deregister content */
            printf("Received DEREGISTER request\n");
            spdu.type = 'A';
            bzero(spdu.data, sizeof(spdu.data));

            /* Remove entry matching BOTH peer and content name */
            for (i = 0; i < content_count; i++) {
                if (strcmp(contents[i].peer_name, request.peer_name) == 0 &&
                    strcmp(contents[i].content_name, request.content_name) == 0) {

                    printf("De-registering peer: %s, content: %s, port: %d\n",
                           contents[i].peer_name,
                           contents[i].content_name,
                           ntohs(contents[i].addr.sin_port));

                    /* shift table entries left by 1 */
                    int j;
                    for (j = i; j < content_count - 1; j++) {
                        contents[j] = contents[j + 1];
                    }

                    /* clear old last entry */
                    bzero(&contents[content_count - 1],
                          sizeof(contents[content_count - 1]));
                    content_count--;
                    break;
                }
            }

            /* Always send back success PDU */
            if (sendto(s, &spdu, sizeof(spdu), 0,
                       (struct sockaddr *)&fsin, sizeof(fsin)) < 0) {
                perror("sendto");
                exit(1);
            }
            break;

        case 'S':   /* Search for content */
            printf("Received SEARCH request for content: %s\n",
                   request.content_name);

            bzero(&response, sizeof(response));
            int found_index = -1;

            /* find first matching entry */
            for (i = 0; i < content_count; i++) {
                if (strcmp(contents[i].content_name,
                           request.content_name) == 0) {
                    found_index = i;
                    break;
                }
            }

            /* Build reply */
            if (found_index == -1) {
                response.type = 'E';  /* not found */
            } else {
                response.type = 'S';
                response.addr = contents[found_index].addr;

                strncpy(response.peer_name,
                        contents[found_index].peer_name, NAME_SIZE - 1);
                response.peer_name[NAME_SIZE - 1] = '\0';

                strncpy(response.content_name,
                        contents[found_index].content_name, NAME_SIZE - 1);
                response.content_name[NAME_SIZE - 1] = '\0';

                printf("Returning server: %s %s port %d\n",
                       response.peer_name,
                       response.content_name,
                       ntohs(response.addr.sin_port));
            }

            /* Send search response */
            if (sendto(s, &response, sizeof(response), 0,
                       (struct sockaddr *)&fsin, sizeof(fsin)) < 0) {
                perror("sendto");
                exit(1);
            }
            break;

        default:
            /* unknown request type */
            printf("Unknown request type '%c'\n", request.type);
            spdu.type = 'E';
            strncpy(spdu.data, "Unknown request type.\n",
                    sizeof(spdu.data) - 1);

            if (sendto(s, &spdu, sizeof(spdu), 0,
                       (struct sockaddr *)&fsin, sizeof(fsin)) < 0) {
                perror("sendto");
                exit(1);
            }
            break;
        }

        /* reset buffers after each loop */
        bzero(spdu.data, sizeof(spdu.data));
        bzero(message, sizeof(message));
    }
}

/* Build list of unique content names for 'O' request */
void obtain_list(struct content contents[MAX_CONTENT], int length, char *msg) {
    char unique_names[MAX_CONTENT][NAME_SIZE];
    int unique_count = 0;
    int i, j;
    int duplicate;

    msg[0] = '\0';

    if (length <= 0)
        return;

    /* Remove duplicate content names */
    for (i = 0; i < length; i++) {
        if (strlen(contents[i].content_name) == 0)
            continue;

        duplicate = 0;
        for (j = 0; j < unique_count; j++) {
            if (strcmp(contents[i].content_name, unique_names[j]) == 0) {
                duplicate = 1;
                break;
            }
        }

        if (!duplicate && unique_count < MAX_CONTENT) {
            strncpy(unique_names[unique_count],
                    contents[i].content_name, NAME_SIZE - 1);
            unique_names[unique_count][NAME_SIZE - 1] = '\0';
            unique_count++;
        }
    }

    /* Build final message containing all content names */
    for (i = 0; i < unique_count; i++) {
        size_t remaining = BUFSIZE - 1 - strlen(msg);
        if (remaining == 0) break;

        size_t to_copy = strnlen(unique_names[i], NAME_SIZE - 1);
        if (to_copy > remaining) to_copy = remaining;

        strncat(msg, unique_names[i], to_copy);

        if (to_copy < remaining) {
            strncat(msg, "\n", 1);   /* newline separated list */
        }
    }

    printf("Current content list:\n%s", msg);
}
