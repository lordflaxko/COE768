
/* Lab 3: TCP File Download - Server
 * Behavior per spec: accept connection, read filename, attempt to open file.
 * If fail: send 'E' + error message. If succeed:
 *   - Enforce payload chunks up to 100 bytes. Each packet: 1 tag byte + payload.
 *   - Optionally enforce "file size > 100 bytes" rule by rejecting too-small files.
 * After sending the file (or error), close the connection.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define SERVER_TCP_PORT 3000
#define BACKLOG         5
#define MAX_PACKET      101   /* 1 tag + up to 100 data bytes (requirement) */
#define PAYLOAD_MAX     100
#define TAG_DATA        'D'
#define TAG_ERR         'E'

static void send_error(int sd, const char *msg) {
    char pkt[MAX_PACKET];
    size_t mlen = strlen(msg);
    if (mlen > PAYLOAD_MAX) mlen = PAYLOAD_MAX;
    pkt[0] = TAG_ERR;
    memcpy(pkt + 1, msg, mlen);
    (void)write(sd, pkt, (size_t)(1 + mlen));
}

int main(int argc, char **argv) {
    int sd = socket(AF_INET, SOCK_STREAM, 0);
    if (sd < 0) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(SERVER_TCP_PORT);

    if (bind(sd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("bind");
        close(sd);
        return 1;
    }

    if (listen(sd, BACKLOG) < 0) {
        perror("listen");
        close(sd);
        return 1;
    }

    fprintf(stdout, "Server listening on port %d...\n", SERVER_TCP_PORT);
    fflush(stdout);

    while (1) {
        struct sockaddr_in cli_addr;
        socklen_t cli_len = sizeof(cli_addr);
        int nsd = accept(sd, (struct sockaddr*)&cli_addr, &cli_len);
        if (nsd < 0) { perror("accept"); continue; }

        /* Read filename (null-terminated string expected) */
        char fname[512];
        memset(fname, 0, sizeof(fname));
        ssize_t n = read(nsd, fname, sizeof(fname) - 1);
        if (n <= 0) {
            send_error(nsd, "No filename received");
            close(nsd);
            continue;
        }

        /* Normalize: strip trailing newline if present */
        size_t flen = strnlen(fname, sizeof(fname));
        if (flen && (fname[flen-1] == '\n' || fname[flen-1] == '\r')) {
            fname[flen-1] = '\0';
        }

        /* Try open file */
        int fd = open(fname, O_RDONLY);
        if (fd < 0) {
            char emsg[256];
            snprintf(emsg, sizeof(emsg), "Cannot open '%s': %s", fname, strerror(errno));
            send_error(nsd, emsg);
            close(nsd);
            continue;
        }

        /* Enforce requirement: file size must be > 100 bytes */
        struct stat st;
        if (fstat(fd, &st) == 0) {
            if (st.st_size <= 100) {
                send_error(nsd, "File too small (must be > 100 bytes)");
                close(fd);
                close(nsd);
                continue;
            }
        }

        /* Stream file in chunks, each packet = [TAG_DATA][up to 100 bytes] */
        char pkt[MAX_PACKET];
        pkt[0] = TAG_DATA;

        for (;;) {
            ssize_t r = read(fd, pkt + 1, PAYLOAD_MAX);
            if (r < 0) {
                char emsg[128];
                snprintf(emsg, sizeof(emsg), "Read error: %s", strerror(errno));
                send_error(nsd, emsg);
                break;
            }
            if (r == 0) {
                /* EOF - done */
                break;
            }
            ssize_t to_write = r + 1;
            ssize_t off = 0;
            while (off < to_write) {
                ssize_t w = write(nsd, pkt + off, (size_t)(to_write - off));
                if (w < 0) {
                    perror("write");
                    off = to_write; /* abort */
                    break;
                }
                off += w;
            }
            /* Loop until EOF */
        }

        close(fd);
        close(nsd);
        /* Go back and accept the next client */
    }

    close(sd);
    return 0;
}
