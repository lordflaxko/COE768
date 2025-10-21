// udp_file_client.c — COE768 Lab 4 Part III (UDP file download client)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>

#define DATA_MAX 100

struct pdu {
    char type;
    char data[DATA_MAX];
};

static void strip_newline(char *s) {
    size_t len = strlen(s);
    if (len && (s[len-1] == '\n')) s[len-1] = '\0';
}

int main(int argc, char *argv[]) {
    int sd;
    struct sockaddr_in sin;

    if (argc != 3) {
        fprintf(stderr, "Usage: %s <server_ip> <port>\n", argv[0]);
        return 1;
    }

    if ((sd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket");
        return 1;
    }

    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons((unsigned short)atoi(argv[2]));
    if (inet_pton(AF_INET, argv[1], &sin.sin_addr) != 1) {
        fprintf(stderr, "Invalid IP address: %s\n", argv[1]);
        return 1;
    }

    if (connect(sd, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
        perror("connect");
        return 1;
    }

    char line[256];
    for (;;) {
        printf("Enter filename to download or 'quit': ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;
        strip_newline(line);
        if (strcmp(line, "quit") == 0) break;
        if (line[0] == '\0') continue;

        struct pdu spdu;
        spdu.type = 'C';
        strncpy(spdu.data, line, DATA_MAX - 1);
        spdu.data[DATA_MAX - 1] = '\0';
        size_t send_len = 1 + strlen(spdu.data);
        if (write(sd, &spdu, send_len) < 0) continue;

        int outfd = -1;
        ssize_t n;
        for (;;) {
            struct pdu rpdu;
            n = read(sd, &rpdu, sizeof(rpdu));
            if (n <= 0) break;
            if (rpdu.type == 'E') {
                rpdu.data[n - 1] = '\0';
                fprintf(stderr, "%s\n", rpdu.data);
                if (outfd >= 0) close(outfd);
                break;
            }
            if (outfd < 0) {
                outfd = open(line, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (outfd < 0) break;
            }
            write(outfd, rpdu.data, n - 1);
            if (rpdu.type == 'F') {
                printf("Downloaded '%s' successfully.\n", line);
                close(outfd);
                break;
            }
        }
    }
    close(sd);
    return 0;
}
