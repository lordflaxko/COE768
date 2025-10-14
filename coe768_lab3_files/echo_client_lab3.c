
/* Lab 3: TCP File Download - Client
 * Behavior per spec: send filename to server, then read packets (<=100B payload each).
 * First byte of each packet is a type tag: 'D' = file data, 'E' = error message.
 * If 'D', write remaining bytes to a local file; if 'E', print the message to stderr.
 *
 * Minimal changes approach: simple, single-file client using POSIX sockets.
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
#include <fcntl.h>

#define SERVER_TCP_PORT 3000
#define MAX_PACKET      101   /* 1 tag byte + up to 100 data bytes */
#define TAG_DATA        'D'
#define TAG_ERR         'E'

static void usage(const char *prog) {
    fprintf(stderr, "Usage: %s <server_ip> <filename_to_request>\n", prog);
}

int main(int argc, char **argv) {
    if (argc != 3) {
        usage(argv[0]);
        return 1;
    }

    const char *server_ip = argv[1];
    const char *req_name   = argv[2];

    int sd = socket(AF_INET, SOCK_STREAM, 0);
    if (sd < 0) { perror("socket"); return 1; }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port   = htons(SERVER_TCP_PORT);
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) != 1) {
        fprintf(stderr, "Invalid server IP: %s\n", server_ip);
        close(sd);
        return 1;
    }

    if (connect(sd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        close(sd);
        return 1;
    }

    /* Send filename (null-terminated) */
    size_t name_len = strlen(req_name) + 1;
    if (write(sd, req_name, name_len) != (ssize_t)name_len) {
        perror("write filename");
        close(sd);
        return 1;
    }

    /* Prepare local output file path: "<requested>.download" */
    char out_name[512];
    snprintf(out_name, sizeof(out_name), "%s.download", req_name);
    int out_fd = -1;
    int saw_data_or_error = 0;

    char buf[MAX_PACKET];
    for (;;) {
        ssize_t n = read(sd, buf, sizeof(buf));
        if (n < 0) {
            perror("read");
            close(sd);
            if (out_fd >= 0) close(out_fd);
            return 1;
        }
        if (n == 0) {
            /* Connection closed: end of transmission */
            break;
        }
        if (n < 1) {
            /* Should not happen: need at least a tag */
            continue;
        }

        char tag = buf[0];
        if (!saw_data_or_error) {
            saw_data_or_error = 1;
            if (tag == TAG_ERR) {
                /* First packet is an error message. Print and then continue reading until close (or stop here). */
                fwrite(buf + 1, 1, (size_t)(n - 1), stderr);
                fputc('\n', stderr);
                /* Drain remaining data, if any, then exit after socket closes */
                continue;
            }
            if (tag == TAG_DATA) {
                /* Create output file only when we confirmed it's data */
                out_fd = open(out_name, O_CREAT | O_TRUNC | O_WRONLY, 0644);
                if (out_fd < 0) {
                    perror("open output");
                    close(sd);
                    return 1;
                }
            } else {
                /* Unknown tag; ignore */
                continue;
            }
        }

        if (tag == TAG_DATA) {
            /* Write payload excluding the 1-byte tag */
            if (out_fd >= 0 && n > 1) {
                ssize_t written = write(out_fd, buf + 1, (size_t)(n - 1));
                if (written < 0) { perror("write file"); break; }
            }
        } else if (tag == TAG_ERR) {
            if (n > 1) {
                fwrite(buf + 1, 1, (size_t)(n - 1), stderr);
            }
        } else {
            /* Unknown tag, ignore */
        }
    }

    if (out_fd >= 0) {
        close(out_fd);
        printf("Downloaded file saved as: %s\n", out_name);
    }
    close(sd);
    return 0;
}
