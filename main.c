#define _POSIX_C_SOURCE 200809L
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <ctype.h>

#define PORT 2020
#define BUF_SIZE 1024

bool is_palindrom(const char *word) {
    for (size_t i = 0; i < strlen(word) / 2; ++i) {
        if (tolower(word[i]) != tolower(word[strlen(word) - i - 1])) {
            return false;
        }
    }
    return true;
}

bool is_valid_word(const char *word) {
    for (int i = 0; word[i] != '\0'; ++i) {
        if (!isalpha((unsigned char)word[i])) {
            return false;
        }
    }
    return true;
}

int main(void)
{
    int sock;
    int rc;         // "rc" to skrót słów "result code"
    ssize_t cnt;    // na wyniki zwracane przez recvfrom() i sendto()

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == -1) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in addr = {
            .sin_family = AF_INET,
            .sin_addr = { .s_addr = htonl(INADDR_ANY) },
            .sin_port = htons(PORT)
    };

    rc = bind(sock, (struct sockaddr *) & addr, sizeof(addr));
    if (rc == -1) {
        perror("bind");
        return 1;
    }

    while (1) {
        unsigned char buf[BUF_SIZE];
        struct sockaddr_in clnt_addr;
        socklen_t clnt_addr_len;

        clnt_addr_len = sizeof(clnt_addr);
        cnt = recvfrom(sock, buf, strlen(buf), 0,
                       (struct sockaddr *) & clnt_addr, & clnt_addr_len);
        if (cnt == -1) {
            perror("recvfrom");
            return 1;
        }
        buf[cnt] = '\0';

        if (cnt > BUF_SIZE - 1) {
            sendto(sock, "ERROR", 5, 0,
                   (struct sockaddr *) & clnt_addr, clnt_addr_len);
        }

        size_t words_nr = 0, pal_nr = 0;
        char *words[100];
        char *token = strtok((char *)buf, " ");

        while(token) {
            words[words_nr++] = token;
            if (!is_valid_word(token)) {
                sendto(sock, "ERROR", 5, 0,
                       (struct sockaddr *) & clnt_addr, clnt_addr_len);
                break;
            }

            if(is_palindrom(token)) {
                pal_nr++;
            }
            token = strtok(NULL, " ");
        }

        char res[20];
        snprintf(res, strlen(res), "%zu/%zu", pal_nr, words_nr);

        cnt = sendto(sock, res, strlen(res), 0,
                     (struct sockaddr *) & clnt_addr, clnt_addr_len);
        if (cnt == -1) {
            perror("sendto");
            return 1;
        }
        printf("sent %zi bytes\n", cnt);
    }

    rc = close(sock);
    if (rc == -1) {
        perror("close");
        return 1;
    }

    return 0;
}