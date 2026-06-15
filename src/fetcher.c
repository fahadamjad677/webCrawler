#include "crawler.h"

/* libcurl write callback: appends data to ResponseBuf */
static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *userdata) {
    ResponseBuf *buf = (ResponseBuf *)userdata;
    size_t incoming = size * nmemb;

    char *tmp = realloc(buf->data, buf->len + incoming + 1);
    if (!tmp) return 0; /* signal error to libcurl */

    buf->data = tmp;
    memcpy(buf->data + buf->len, ptr, incoming);
    buf->len += incoming;
    buf->data[buf->len] = '\0';
    return incoming;
}

/*
 * fetch_url: performs an HTTP GET of `url` and stores the response
 * body in `buf`. Caller must call response_buf_free() afterwards.
 *
 * Returns 0 on success, non-zero on error.
 */
int fetch_url(const char *url, ResponseBuf *buf) {
    buf->data = NULL;
    buf->len  = 0;

    CURL *curl = curl_easy_init();
    if (!curl) return -1;

    CURLcode res;

    curl_easy_setopt(curl, CURLOPT_URL,            url);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS,      5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,        10L);       /* 10-second total timeout */
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,  write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,      buf);

    /* Identify as a crawler */
    curl_easy_setopt(curl, CURLOPT_USERAGENT,
                     "OS-Lab-Crawler/1.0 (Educational Project)");

    /* Accept only text/html to avoid downloading binary files */
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Accept: text/html");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    res = curl_easy_perform(curl);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        free(buf->data);
        buf->data = NULL;
        buf->len  = 0;
        return (int)res;
    }
    return 0;
}

void response_buf_free(ResponseBuf *buf) {
    if (buf->data) {
        free(buf->data);
        buf->data = NULL;
        buf->len  = 0;
    }
}
