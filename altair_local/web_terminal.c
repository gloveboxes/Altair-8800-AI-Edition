/**
 * @file web_terminal.c
 * @brief Browser terminal bridge for the local Altair runner (see header).
 */

#include "web_terminal.h"

#if !defined(_WIN32) && defined(ALTAIR_LOCAL_HAVE_WEB_TERMINAL)

#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif

#include <ctype.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <ws.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

/* Output queued for browsers; sized for burst output (screen clears, LIST). */
#define WT_TX_CAP 16384
/* Input from browsers; ample for fast typing / paste. */
#define WT_RX_CAP 4096
/* Max bytes per WebSocket frame the TX thread emits (matches ESP32 batch). */
#define WT_TX_BATCH 2048
/* TX batching cadence: collect output briefly so each frame carries more. */
#define WT_TX_TICK_MS 15
/* Keepalive ping cadence (loopback rarely needs it, but cheap insurance). */
#define WT_PING_INTERVAL_MS 30000
/* Largest HTTP request head we will buffer before serving / proxying. */
#define WT_REQ_HEAD_MAX 8192

/* ---- Configuration / lifecycle state -------------------------------------- */

static bool s_active = false;
static uint16_t s_public_port = 0;
static uint16_t s_ws_port = 0;

static char *s_html = NULL;     /* terminal/index.html contents */
static size_t s_html_len = 0;

static int s_listen_fd = -1;
static pthread_t s_accept_thread;
static bool s_accept_thread_started = false;
static pthread_t s_tx_thread;
static bool s_tx_thread_started = false;

/* ---- Client tracking ------------------------------------------------------ */

static pthread_mutex_t s_state_lock = PTHREAD_MUTEX_INITIALIZER;
static int s_client_count = 0;

/* ---- Output ring (emulator -> browsers) ----------------------------------- */

static pthread_mutex_t s_tx_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_tx_cond = PTHREAD_COND_INITIALIZER;
static uint8_t s_tx_buf[WT_TX_CAP];
static size_t s_tx_head = 0; /* next read */
static size_t s_tx_tail = 0; /* next write */
static size_t s_tx_count = 0;

/* ---- Input ring (browsers -> emulator) ------------------------------------ */

static pthread_mutex_t s_rx_lock = PTHREAD_MUTEX_INITIALIZER;
static uint8_t s_rx_buf[WT_RX_CAP];
static size_t s_rx_head = 0;
static size_t s_rx_tail = 0;
static size_t s_rx_count = 0;

/* ---- Small helpers -------------------------------------------------------- */

static uint64_t monotonic_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000ULL);
}

/* Case-insensitive substring search (strcasestr is not portable). */
static const char *ci_strstr(const char *haystack, const char *needle)
{
    size_t nlen = strlen(needle);
    if (nlen == 0)
    {
        return haystack;
    }
    for (; *haystack; haystack++)
    {
        size_t i = 0;
        while (i < nlen && haystack[i] &&
               tolower((unsigned char)haystack[i]) == tolower((unsigned char)needle[i]))
        {
            i++;
        }
        if (i == nlen)
        {
            return haystack;
        }
    }
    return NULL;
}

static bool send_all(int fd, const void *buf, size_t len)
{
    const char *p = (const char *)buf;
    while (len > 0)
    {
        ssize_t n = send(fd, p, len, MSG_NOSIGNAL);
        if (n < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            return false;
        }
        if (n == 0)
        {
            return false;
        }
        p += n;
        len -= (size_t)n;
    }
    return true;
}

/* ---- wsServer event handlers ---------------------------------------------- */

static void on_ws_open(ws_cli_conn_t client)
{
    (void)client;
    pthread_mutex_lock(&s_state_lock);
    s_client_count++;
    pthread_mutex_unlock(&s_state_lock);

    /* Drop any stale output buffered before this client attached. */
    web_terminal_clear_tx();
}

static void on_ws_close(ws_cli_conn_t client)
{
    (void)client;
    pthread_mutex_lock(&s_state_lock);
    if (s_client_count > 0)
    {
        s_client_count--;
    }
    pthread_mutex_unlock(&s_state_lock);
}

static void on_ws_message(ws_cli_conn_t client, const unsigned char *msg,
                          uint64_t size, int type)
{
    (void)client;
    (void)type;
    if (msg == NULL || size == 0)
    {
        return;
    }

    pthread_mutex_lock(&s_rx_lock);
    for (uint64_t i = 0; i < size; i++)
    {
        if (s_rx_count == WT_RX_CAP)
        {
            /* Drop oldest so the most recent keystrokes always get through. */
            s_rx_head = (s_rx_head + 1) % WT_RX_CAP;
            s_rx_count--;
        }
        s_rx_buf[s_rx_tail] = msg[i];
        s_rx_tail = (s_rx_tail + 1) % WT_RX_CAP;
        s_rx_count++;
    }
    pthread_mutex_unlock(&s_rx_lock);
}

/* ---- TX batching thread --------------------------------------------------- */

static void *tx_thread_fn(void *arg)
{
    (void)arg;
    uint8_t batch[WT_TX_BATCH];
    uint64_t last_ping = monotonic_ms();

    while (s_active)
    {
        size_t n = 0;

        pthread_mutex_lock(&s_tx_lock);
        while (s_tx_count == 0 && s_active)
        {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            uint64_t target_ns = (uint64_t)ts.tv_nsec + (uint64_t)WT_TX_TICK_MS * 1000000ULL;
            ts.tv_sec += (time_t)(target_ns / 1000000000ULL);
            ts.tv_nsec = (long)(target_ns % 1000000000ULL);
            pthread_cond_timedwait(&s_tx_cond, &s_tx_lock, &ts);
            break; /* re-evaluate loop conditions and ping timer below */
        }
        while (n < WT_TX_BATCH && s_tx_count > 0)
        {
            batch[n++] = s_tx_buf[s_tx_head];
            s_tx_head = (s_tx_head + 1) % WT_TX_CAP;
            s_tx_count--;
        }
        pthread_mutex_unlock(&s_tx_lock);

        if (!s_active)
        {
            break;
        }

        if (n > 0 && web_terminal_has_clients())
        {
            ws_sendframe_bin_bcast(s_ws_port, (const char *)batch, n);
        }

        uint64_t now = monotonic_ms();
        if (now - last_ping >= WT_PING_INTERVAL_MS)
        {
            last_ping = now;
            if (web_terminal_has_clients())
            {
                /* cid 0 is never a valid client id -> wsServer broadcasts. */
                ws_ping((ws_cli_conn_t)0, 3);
            }
        }
    }
    return NULL;
}

/* ---- HTTP / reverse-proxy connection handling ----------------------------- */

static void serve_index_html(int fd, const char *path)
{
    bool is_root = (strcmp(path, "/") == 0) || (strcmp(path, "/index.html") == 0);
    if (!is_root)
    {
        static const char not_found[] =
            "HTTP/1.1 404 Not Found\r\n"
            "Content-Length: 0\r\n"
            "Connection: close\r\n\r\n";
        send_all(fd, not_found, sizeof(not_found) - 1);
        return;
    }

    char header[256];
    int hlen = snprintf(header, sizeof(header),
                        "HTTP/1.1 200 OK\r\n"
                        "Content-Type: text/html; charset=utf-8\r\n"
                        "Content-Length: %zu\r\n"
                        "Cache-Control: no-cache\r\n"
                        "Connection: close\r\n\r\n",
                        s_html_len);
    if (hlen > 0 && send_all(fd, header, (size_t)hlen))
    {
        send_all(fd, s_html, s_html_len);
    }
}

/* Pump bytes in both directions between the browser (a) and wsServer (b)
   until either side closes. wsServer handles all WebSocket framing; this is a
   transparent TCP relay. */
static void proxy_pump(int a, int b)
{
    char buf[4096];
    for (;;)
    {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(a, &rfds);
        FD_SET(b, &rfds);
        int maxfd = (a > b ? a : b) + 1;

        struct timeval tv = {.tv_sec = 1, .tv_usec = 0};
        int r = select(maxfd, &rfds, NULL, NULL, &tv);
        if (r < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            break;
        }
        if (!s_active)
        {
            break;
        }
        if (r == 0)
        {
            continue;
        }

        if (FD_ISSET(a, &rfds))
        {
            ssize_t n = recv(a, buf, sizeof(buf), 0);
            if (n <= 0 || !send_all(b, buf, (size_t)n))
            {
                break;
            }
        }
        if (FD_ISSET(b, &rfds))
        {
            ssize_t n = recv(b, buf, sizeof(buf), 0);
            if (n <= 0 || !send_all(a, buf, (size_t)n))
            {
                break;
            }
        }
    }
}

static int connect_loopback(uint16_t port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
    {
        return -1;
    }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        close(fd);
        return -1;
    }
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    return fd;
}

static void *connection_thread_fn(void *arg)
{
    int fd = (int)(intptr_t)arg;

    /* Don't let a stalled peer pin this thread forever while reading headers. */
    struct timeval rcv = {.tv_sec = 10, .tv_usec = 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &rcv, sizeof(rcv));

    char head[WT_REQ_HEAD_MAX + 1];
    size_t hl = 0;
    bool have_head = false;

    while (hl < WT_REQ_HEAD_MAX)
    {
        ssize_t n = recv(fd, head + hl, WT_REQ_HEAD_MAX - hl, 0);
        if (n <= 0)
        {
            break;
        }
        hl += (size_t)n;
        head[hl] = '\0';
        if (strstr(head, "\r\n\r\n") != NULL)
        {
            have_head = true;
            break;
        }
    }

    if (!have_head)
    {
        close(fd);
        return NULL;
    }

    if (ci_strstr(head, "upgrade: websocket") != NULL)
    {
        /* WebSocket upgrade: relay verbatim to the loopback wsServer, which
           performs the handshake and all framing. */
        int up = connect_loopback(s_ws_port);
        if (up < 0)
        {
            close(fd);
            return NULL;
        }
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        /* Restore blocking semantics for the relay (no header read timeout). */
        struct timeval none = {0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &none, sizeof(none));

        if (send_all(up, head, hl))
        {
            proxy_pump(fd, up);
        }
        close(up);
        close(fd);
        return NULL;
    }

    /* Plain HTTP request: serve the terminal UI. */
    char method[8] = {0};
    char path[1024] = {0};
    sscanf(head, "%7s %1023s", method, path);
    if (strcmp(method, "GET") == 0)
    {
        serve_index_html(fd, path);
    }
    else
    {
        static const char bad[] =
            "HTTP/1.1 405 Method Not Allowed\r\n"
            "Content-Length: 0\r\n"
            "Connection: close\r\n\r\n";
        send_all(fd, bad, sizeof(bad) - 1);
    }
    close(fd);
    return NULL;
}

static void *accept_thread_fn(void *arg)
{
    (void)arg;
    while (s_active)
    {
        int fd = accept(s_listen_fd, NULL, NULL);
        if (fd < 0)
        {
            if (!s_active)
            {
                break;
            }
            if (errno == EINTR)
            {
                continue;
            }
            break;
        }

        pthread_t t;
        if (pthread_create(&t, NULL, connection_thread_fn, (void *)(intptr_t)fd) != 0)
        {
            close(fd);
            continue;
        }
        pthread_detach(t);
    }
    return NULL;
}

/* ---- File loading --------------------------------------------------------- */

static bool load_index_html(const char *repo_root)
{
    char path[2048];

    /* ALTAIR_WEB_ROOT lets a deployment (e.g. the Docker image) override where
       terminal/index.html lives, since the compiled-in repo root points at the
       build machine's source tree and won't exist at runtime. */
    const char *override_root = getenv("ALTAIR_WEB_ROOT");
    if (override_root != NULL && override_root[0] != '\0')
    {
        repo_root = override_root;
    }

    snprintf(path, sizeof(path), "%s/terminal/index.html", repo_root);

    FILE *f = fopen(path, "rb");
    if (f == NULL)
    {
        fprintf(stderr, "[web-terminal] cannot open %s: %s\n", path, strerror(errno));
        return false;
    }
    if (fseek(f, 0, SEEK_END) != 0)
    {
        fclose(f);
        return false;
    }
    long size = ftell(f);
    if (size < 0)
    {
        fclose(f);
        return false;
    }
    rewind(f);

    char *buf = (char *)malloc((size_t)size);
    if (buf == NULL)
    {
        fclose(f);
        return false;
    }
    size_t read = fread(buf, 1, (size_t)size, f);
    fclose(f);
    if (read != (size_t)size)
    {
        free(buf);
        return false;
    }

    free(s_html);
    s_html = buf;
    s_html_len = (size_t)size;
    return true;
}

/* ---- Public API ----------------------------------------------------------- */

bool web_terminal_start(const char *repo_root, uint16_t port)
{
    if (s_active)
    {
        return true;
    }
    if (repo_root == NULL || port == 0)
    {
        return false;
    }

    /* Writing to a socket whose peer has gone away must not kill the process. */
    signal(SIGPIPE, SIG_IGN);

    if (!load_index_html(repo_root))
    {
        return false;
    }

    s_public_port = port;
    s_ws_port = (port < 65535) ? (uint16_t)(port + 1) : (uint16_t)(port - 1);

    /* Start wsServer on the loopback internal port (it owns its own threads). */
    static struct ws_server srv; /* wsServer copies this, but keep it alive. */
    memset(&srv, 0, sizeof(srv));
    srv.host = "127.0.0.1";
    srv.port = s_ws_port;
    srv.thread_loop = 1;
    srv.timeout_ms = 1000;
    srv.evs.onopen = on_ws_open;
    srv.evs.onclose = on_ws_close;
    srv.evs.onmessage = on_ws_message;
    if (ws_socket(&srv) != 0)
    {
        fprintf(stderr, "[web-terminal] failed to start wsServer on port %u\n", s_ws_port);
        return false;
    }

    /* Public HTTP / proxy listener. */
    s_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (s_listen_fd < 0)
    {
        fprintf(stderr, "[web-terminal] socket() failed: %s\n", strerror(errno));
        return false;
    }
    int one = 1;
    setsockopt(s_listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(s_listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        fprintf(stderr, "[web-terminal] bind(:%u) failed: %s\n", port, strerror(errno));
        close(s_listen_fd);
        s_listen_fd = -1;
        return false;
    }
    if (listen(s_listen_fd, 8) < 0)
    {
        fprintf(stderr, "[web-terminal] listen() failed: %s\n", strerror(errno));
        close(s_listen_fd);
        s_listen_fd = -1;
        return false;
    }

    s_active = true;

    if (pthread_create(&s_tx_thread, NULL, tx_thread_fn, NULL) != 0)
    {
        s_active = false;
        close(s_listen_fd);
        s_listen_fd = -1;
        return false;
    }
    s_tx_thread_started = true;

    if (pthread_create(&s_accept_thread, NULL, accept_thread_fn, NULL) != 0)
    {
        s_active = false;
        pthread_cond_broadcast(&s_tx_cond);
        pthread_join(s_tx_thread, NULL);
        s_tx_thread_started = false;
        close(s_listen_fd);
        s_listen_fd = -1;
        return false;
    }
    s_accept_thread_started = true;

    return true;
}

void web_terminal_stop(void)
{
    if (!s_active)
    {
        return;
    }
    s_active = false;

    /* Wake and break the accept loop. */
    if (s_listen_fd >= 0)
    {
        shutdown(s_listen_fd, SHUT_RDWR);
        close(s_listen_fd);
        s_listen_fd = -1;
    }
    if (s_accept_thread_started)
    {
        pthread_join(s_accept_thread, NULL);
        s_accept_thread_started = false;
    }

    /* Wake and stop the TX thread. */
    pthread_mutex_lock(&s_tx_lock);
    pthread_cond_broadcast(&s_tx_cond);
    pthread_mutex_unlock(&s_tx_lock);
    if (s_tx_thread_started)
    {
        pthread_join(s_tx_thread, NULL);
        s_tx_thread_started = false;
    }

    free(s_html);
    s_html = NULL;
    s_html_len = 0;
    /* wsServer's accept thread is detached and not stoppable; the process is
       exiting, so the OS reclaims it. */
}

bool web_terminal_active(void)
{
    return s_active;
}

bool web_terminal_has_clients(void)
{
    bool has;
    pthread_mutex_lock(&s_state_lock);
    has = s_client_count > 0;
    pthread_mutex_unlock(&s_state_lock);
    return has;
}

void web_terminal_tx_byte(uint8_t value)
{
    if (!s_active)
    {
        return;
    }
    if (!web_terminal_has_clients())
    {
        return; /* No browser attached; output is dropped (cleared on connect). */
    }

    pthread_mutex_lock(&s_tx_lock);
    if (s_tx_count == WT_TX_CAP)
    {
        /* Drop oldest byte to make room for the newest. */
        s_tx_head = (s_tx_head + 1) % WT_TX_CAP;
        s_tx_count--;
    }
    s_tx_buf[s_tx_tail] = value;
    s_tx_tail = (s_tx_tail + 1) % WT_TX_CAP;
    s_tx_count++;
    pthread_cond_signal(&s_tx_cond);
    pthread_mutex_unlock(&s_tx_lock);
}

void web_terminal_clear_tx(void)
{
    pthread_mutex_lock(&s_tx_lock);
    s_tx_head = s_tx_tail = s_tx_count = 0;
    pthread_mutex_unlock(&s_tx_lock);
}

bool web_terminal_rx_byte(uint8_t *out)
{
    bool got = false;
    if (out == NULL)
    {
        return false;
    }
    pthread_mutex_lock(&s_rx_lock);
    if (s_rx_count > 0)
    {
        *out = s_rx_buf[s_rx_head];
        s_rx_head = (s_rx_head + 1) % WT_RX_CAP;
        s_rx_count--;
        got = true;
    }
    pthread_mutex_unlock(&s_rx_lock);
    return got;
}

#else /* _WIN32 or wsServer submodule missing: stubs */

bool web_terminal_start(const char *repo_root, uint16_t port)
{
    (void)repo_root;
    (void)port;
    return false;
}

void web_terminal_stop(void) {}
bool web_terminal_active(void) { return false; }
bool web_terminal_has_clients(void) { return false; }
void web_terminal_tx_byte(uint8_t value) { (void)value; }
void web_terminal_clear_tx(void) {}

bool web_terminal_rx_byte(uint8_t *out)
{
    (void)out;
    return false;
}

#endif
