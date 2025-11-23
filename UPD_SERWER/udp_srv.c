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
    for (size_t i = 0; i < strlen(word) / 2; i++) {
        if (tolower(word[i]) != tolower(word[strlen(word) - i - 1])) {
            return false;
        }
    }
    return true;
}

bool is_valid_word(const char *word) {
    for (int i = 0; word[i] != '\0'; i++) {
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
		memset(buf, 0, BUF_SIZE);
        struct sockaddr_in clnt_addr;
        socklen_t clnt_addr_len;

        clnt_addr_len = sizeof(clnt_addr);
        cnt = recvfrom(sock, buf, BUF_SIZE - 1, 0,
                       (struct sockaddr *) & clnt_addr, & clnt_addr_len);
        if (cnt == -1) {
            perror("recvfrom");
            continue;
        }
		        
        bool has_null = false;
        for (ssize_t i = 0; i < cnt - 1; i++) {  
            if (buf[i] == '\0') {
                has_null = true;
                break;
            }
        }

        if (has_null) {
            cnt = sendto(sock, "ERROR", 6, 0, (struct sockaddr *)&clnt_addr, clnt_addr_len);
            if (cnt == -1) {
                perror("sendto");
            }
            continue;
        } 

		//const char *tmp;
		//strncpy(tmp, (const char *)buf, strlen(buf) - 1);
		/* size_t len = cnt;
		while ( len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r')) {
			buf[len-1] = '\0';
			len--;
		} */
	
		buf[cnt] = '\0';
		//printf("Received: %s (size %ld)\n", buf, cnt);
        
        if (cnt >= BUF_SIZE - 1) {
            cnt = sendto(sock, "ERROR", 6, 0,
                   (struct sockaddr *) & clnt_addr, clnt_addr_len);
			//printf("ERROR\n");
			if (cnt == -1) {
				perror("sendto");
			}
			continue;
        }

		char buf_copy[BUF_SIZE];
		strncpy(buf_copy, (const char *)buf, BUF_SIZE);
		buf_copy[BUF_SIZE - 1] = '\0';

        size_t words_nr = 0, pal_nr = 0;
        char *token = strtok(buf_copy, " ");
		bool returned_error = false;
		
		if (buf[0] == ' ' || buf[strlen((char *)buf) - 1] == ' ') {
    		cnt = sendto(sock, "ERROR", 6, 0, (struct sockaddr *) & clnt_addr, clnt_addr_len);
    		//printf("ERROR\n");
			if (cnt == -1) {
				perror("sendto");
			}
			returned_error = true;
    		continue;
		}

        while(token) {
            words_nr++;
            if (!is_valid_word(token)) {
                cnt = sendto(sock, "ERROR", 6, 0,
                       (struct sockaddr *) & clnt_addr, clnt_addr_len);
				if (cnt == -1) {
					perror("sendto");
				}
                returned_error = true;
                //printf("ERROR\n");
				break;
            }

            if(is_palindrom(token)) {
                pal_nr++;
            }
            
            char *next_token = strtok(NULL, " ");
    		if (next_token && *(next_token-1) == ' ') {
        		cnt = sendto(sock, "ERROR", 6, 0, (struct sockaddr *) & clnt_addr, clnt_addr_len);
				if (cnt == -1) {
					perror("sendto");
				}
        		returned_error = true;
        		//printf("ERROR\n");
        		break;
    		}

    		token = next_token;
        }

		if (!returned_error){

			//printf("Palindromes: %zu, Words: %zu\n", pal_nr, words_nr);

			char res[20];
			snprintf(res, sizeof(res), "%zu/%zu", pal_nr, words_nr);

			cnt = sendto(sock, res, strlen(res), 0,
		             (struct sockaddr *) & clnt_addr, clnt_addr_len);
			if (cnt == -1) {
		    	perror("sendto");
		    	continue;
			}
			//printf("sent: %s (%zi bytes)\n", res, cnt);
    	}
	}

    rc = close(sock);
    if (rc == -1) {
        perror("close");
        return 1;
    }

    return 0;
}

