// udp_file_server.c — COE768 Lab 4 Part III (UDP file download server)
// Minimal, follows lab manual structure and PDU format.
// Fixes: NUL-terminate filename received from client before printing/opening.
//
// Usage: ./udp_file_server <port>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#define DATA_MAX 100

struct pdu {
    char type;          // 'C' filename, 'D' data, 'F' final, 'E' error
    char data[DATA_MAX];
};

static void die(const char *msg) {
    perror(msg);
    exit(1);
}

int main(int argc, char *argv[]) {
    int sd;
    struct sockaddr_in sin, fsin;
    socklen_t alen = sizeof(fsin);

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return 1;
    }

    // 1) socket
    if ((sd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) die("socket");

    // 2) bind
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(INADDR_ANY);
    sin.sin_port = htons((unsigned short)atoi(argv[1]));

    if (bind(sd, (struct sockaddr *)&sin, sizeof(sin)) < 0) die("bind");

    // (Included per lab call sequence; harmless for UDP if unsupported)
    listen(sd, 5);

    fprintf(stdout, "UDP file server ready on port %s\n", argv[1]);

    for (;;) {
        struct pdu rpdu;
        ssize_t n = recvfrom(sd, &rpdu, sizeof(rpdu), 0,
                             (struct sockaddr *)&fsin, &alen);
        if (n < 0) {
            perror("recvfrom");
            continue;
        }
        if (n < 1) continue;             // must have at least the 'type' byte
        if (rpdu.type != 'C') continue;  // only handle filename PDUs here

        // ---- Fix: make the filename a clean C-string ----
        size_t payload = (n > 1) ? (size_t)(n - 1) : 0;   // bytes of filename
        if (payload >= DATA_MAX) payload = DATA_MAX - 1;  // clamp to buffer
        rpdu.data[payload] = '\0';                        // NUL-terminate

        // optional: strip trailing newline/space just in case
        while (payload && (rpdu.data[payload - 1] == '\n' ||
                           rpdu.data[payload - 1] == '\r' ||
                           rpdu.data[payload - 1] == ' ')) {
            rpdu.data[--payload] = '\0';
        }
        // --------------------------------------------------

        printf("server received filename request: '%s'\n", rpdu.data);

        // Open the requested file
        int fd = open(rpdu.data, O_RDONLY);
        if (fd < 0) {
            struct pdu epdu;
            epdu.type = 'E';
            snprintf(epdu.data, DATA_MAX, "ERROR: %s", strerror(errno));
            sendto(sd, &epdu, 1 + strnlen(epdu.data, DATA_MAX), 0,
                   (struct sockaddr *)&fsin, alen);
            continue;
        }

        // Determine file size so we know when to send FINAL
        struct stat st;
        off_t total = 0, fsize = 0;
        if (fstat(fd, &st) == 0) fsize = st.st_size;

        // Send the file in ≤100-byte chunks; last one marked 'F'
        for (;;) {
            struct pdu dpdu;
            ssize_t r = read(fd, dpdu.data, DATA_MAX);
            if (r < 0) {
                struct pdu epdu;
                epdu.type = 'E';
                snprintf(epdu.data, DATA_MAX, "ERROR: read failed");
                sendto(sd, &epdu, 1 + strnlen(epdu.data, DATA_MAX), 0,
                       (struct sockaddr *)&fsin, alen);
                break;
            }
            if (r == 0) {
                // If nothing was sent yet (empty file) or we reached exact EOF,
                // send a zero-payload FINAL marker.
                dpdu.type = 'F';
                sendto(sd, &dpdu, 1, 0, (struct sockaddr *)&fsin, alen);
                break;
            }

            total += r;
            dpdu.type = (fsize > 0 && total >= fsize) ? 'F' : 'D';

            sendto(sd, &dpdu, 1 + r, 0, (struct sockaddr *)&fsin, alen);

            if (dpdu.type == 'F') break;
        }

        close(fd);
    }

    return 0;
}
