#define _GNU_SOURCE

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <ctype.h>
#include <sys/epoll.h>
#include <sys/syscall.h>

// Standardowa procedura tworząca nasłuchujące gniazdko TCP.

int listening_socket_tcp_ipv4(in_port_t port)
{
    int s;

    if ((s = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        return -1;
    }

    struct sockaddr_in a = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port = htons(port)
    };

    if (bind(s, (struct sockaddr *) &a, sizeof(a)) == -1) {
        perror("bind");
        goto close_and_fail;
    }

    if (listen(s, 128) == -1) {
        perror("listen");
        goto close_and_fail;
    }

    return s;

close_and_fail:
    close(s);
    return -1;
}

// Pomocnicze funkcje do drukowania na ekranie komunikatów uzupełnianych
// o znacznik czasu oraz identyfikatory procesu/wątku. Będą używane do
// raportowania przebiegu pozostałych operacji we-wy.

void log_printf(const char * fmt, ...)
{
    // bufor na przyrostowo budowany komunikat, len mówi ile już znaków ma
    char buf[1024];
    int len = 0;

    struct timespec date_unix;
    struct tm date_human;
    long int usec;
    if (clock_gettime(CLOCK_REALTIME, &date_unix) == -1) {
        perror("clock_gettime");
        return;
    }
    if (localtime_r(&date_unix.tv_sec, &date_human) == NULL) {
        perror("localtime_r");
        return;
    }
    usec = date_unix.tv_nsec / 1000;

    // getpid() i gettid() zawsze wykonują się bezbłędnie
    pid_t pid = getpid();
    pid_t tid = syscall(SYS_gettid);

    len = snprintf(buf, sizeof(buf), "%02i:%02i:%02i.%06li PID=%ji TID=%ji ",
                date_human.tm_hour, date_human.tm_min, date_human.tm_sec,
                usec, (intmax_t) pid, (intmax_t) tid);
    if (len < 0) {
        return;
    }

    va_list ap;
    va_start(ap, fmt);
    int i = vsnprintf(buf + len, sizeof(buf) - len, fmt, ap);
    va_end(ap);
    if (i < 0) {
        return;
    }
    len += i;

    // zamień \0 kończące łańcuch na \n i wyślij całość na stdout, czyli na
    // deskryptor nr 1; bez obsługi błędów aby nie komplikować przykładu
    buf[len] = '\n';
    write(1, buf, len + 1);
}

void log_perror(const char * msg)
{
    log_printf("%s: %s", msg, strerror(errno));
}

void log_error(const char * msg, int errnum)
{
    log_printf("%s: %s", msg, strerror(errnum));
}

// Pomocnicze funkcje wykonujące pojedynczą operację we-wy oraz wypisujące
// szczegółowe komunikaty o jej działaniu.

int accept_verbose(int srv_sock)
{
    struct sockaddr_in a;
    socklen_t a_len = sizeof(a);

    log_printf("calling accept() on descriptor %i", srv_sock);
    int rv = accept(srv_sock, (struct sockaddr *) &a, &a_len);
    if (rv == -1) {
        log_perror("accept");
    } else {
        char buf[INET_ADDRSTRLEN];
        if (inet_ntop(AF_INET, &a.sin_addr, buf, sizeof(buf)) == NULL) {
            log_perror("inet_ntop");
            strcpy(buf, "???");
        }
        log_printf("new client %s:%u, new descriptor %i",
                        buf, (unsigned int) ntohs(a.sin_port), rv);
    }
    return rv;
}

ssize_t read_verbose(int fd, void * buf, size_t nbytes)
{
    log_printf("calling read() on descriptor %i", fd);
    ssize_t rv = read(fd, buf, nbytes);
    if (rv == -1) {
        log_perror("read");
    } else {
        log_printf("received %zi bytes on descriptor %i", rv, fd);
    }
    return rv;
}

ssize_t write_verbose(int fd, void * buf, size_t nbytes)
{
    log_printf("calling write() on descriptor %i", fd);
    ssize_t rv = write(fd, buf, nbytes);
    if (rv == -1) {
        log_perror("write");
    } else if (rv < nbytes) {
        log_printf("partial write on %i, wrote only %zi of %zu bytes",
                        fd, rv, nbytes);
    } else {
        log_printf("wrote %zi bytes on descriptor %i", rv, fd);
    }
    return rv;
}

int close_verbose(int fd)
{
    log_printf("closing descriptor %i", fd);
    int rv = close(fd);
    if (rv == -1) {
        log_perror("close");
    }
    return rv;
}

// Procedury przetwarzające pojedynczą porcję danych przysłaną przez klienta.

bool is_palindrome(const char *word, size_t len) {
    size_t i = 0, j = len - 1;
    while (i < j) {
        if (tolower(word[i]) != tolower(word[j]))
            return false;
        i++;
        j--;
    }
    return true;
}

bool is_valid_line(const char *line) {
    size_t len = strlen(line);

    if (line[0] == ' ' || line[len - 1] == ' ') {
        return false;
    }

    for (size_t i = 0; i < len; ++i) {
        if (line[i] == ' ') {
            if (i > 0 && line[i - 1] == ' ') {
                return false; 
            }
        } else if (!isalpha((unsigned char)line[i])) {
            return false; 
        }
    }

    return true;
}


ssize_t process_line_palindrome(int sock, char *buffer, size_t *buffer_len) {
    ssize_t bytes_read = read_verbose(sock, buffer + *buffer_len, 1024 - *buffer_len);
    if (bytes_read <= 0) {
        return -1;
    }

    //printf("Dostałem: %s\n", buffer);

    if (memchr(buffer + *buffer_len, '\0', bytes_read) != NULL) {
        write_verbose(sock, "ERROR\r\n", 7);
        // *buffer_len = 0;
        // buffer[0] = '\0';

        return -1;
    }

    *buffer_len += bytes_read;
    buffer[*buffer_len] = '\0';

    // Szukamy kompletnej linii zakończonej \r\n
    char *line_end;
    while ((line_end = strstr(buffer, "\r\n")) != NULL) {
        size_t line_len = line_end - buffer;
        char line[1024];
        strncpy(line, buffer, line_len);
        line[line_len] = '\0';

        // Przesuwamy pozostałe dane do początku bufora
        memmove(buffer, line_end + 2, *buffer_len - (line_len + 2));
        *buffer_len -= (line_len + 2);
        buffer[*buffer_len] = '\0';

        // Walidacja
        if (!is_valid_line(line)) {
            write_verbose(sock, "ERROR\r\n", 7);
            continue;
        }

        // Zliczanie palindromów
        int total = 0, palindromes = 0;
        char *saveptr;
        char *token = strtok_r(line, " ", &saveptr);
        while (token) {
            size_t len = strlen(token);
            if (len > 0) {
                ++total;
                if (is_palindrome(token, len)) {
                    ++palindromes;
                }
            }
            token = strtok_r(NULL, " ", &saveptr);
        }

        // Tworzenie odpowiedzi
        char response[64];
        if (total == 0) {
            snprintf(response, sizeof(response), "0/0\r\n");
        } else {
            snprintf(response, sizeof(response), "%d/%d\r\n", palindromes, total);
        }
        write_verbose(sock, response, strlen(response));
    }

    return bytes_read;
}

struct client_buffer {
    char buf[1024];
    size_t len;
};

struct client_buffer client_buffers[FD_SETSIZE];

void select_loop(int srv_sock)
{
    fd_set sock_fds;    // zbiór deskryptorów otwartych gniazdek
    int max_fd;         // największy użyty numer deskryptora

    // na początku zbiór zawiera tylko gniazdko odbierające połączenia
    FD_ZERO(&sock_fds);
    FD_SET(srv_sock, &sock_fds);
    max_fd = srv_sock;

    while (true) {
        log_printf("calling select()");
        // select() modyfikuje zawartość przekazanego mu zbioru, zostaną
        // w nim tylko deskryptory mające gotowe do odczytu dane
        fd_set read_ready_fds = sock_fds;
        int num = select(max_fd + 1, &read_ready_fds, NULL, NULL, NULL);
        if (num == -1) {
            log_perror("select");
            break;
        }
        log_printf("number of ready to read descriptors = %i", num);

        for (int fd = 0; fd <= max_fd; ++fd) {
            if (! FD_ISSET(fd, &read_ready_fds)) {
                continue;
            }

            if (fd == srv_sock) {

                int s = accept_verbose(srv_sock);
                if (s == -1) {
                    goto break_out_of_main_loop;
                } else if (s >= FD_SETSIZE) {
                    log_printf("%i cannot be added to a fd_set", s);
                    // tego klienta nie da się obsłużyć, więc zamknij gniazdko
                    close_verbose(s);
                    continue;
                }

                memset(&client_buffers[s], 0, sizeof(struct client_buffer));
                FD_SET(s, &sock_fds);
                if (s > max_fd) {
                    max_fd = s;
                }

            } else {    // fd != srv_sock

                if (process_line_palindrome(fd, client_buffers[fd].buf, &client_buffers[fd].len) <= 0) {
                    FD_CLR(fd, &sock_fds);
                    close_verbose(fd);
                }
                

            }
        }
    }
break_out_of_main_loop:

    // zamknij wszystkie połączenia z klientami
    for (int fd = 0; fd <= max_fd; ++fd) {
        if (FD_ISSET(fd, &sock_fds) && fd != srv_sock) {
            close_verbose(fd);
        }
    }
}

int main(int argc, char * argv[])
{
    long int srv_port = 2020;
    int srv_sock;
    void (*main_loop)(int) = select_loop;

    srv_sock = listening_socket_tcp_ipv4(srv_port);
    if (srv_sock == -1) {
        return 1;
    }

    log_printf("starting main loop");
    main_loop(srv_sock);
    log_printf("main loop done");

    if (close(srv_sock) == -1) {
        log_perror("close");
        return 1;
    }

    return 0;
}
