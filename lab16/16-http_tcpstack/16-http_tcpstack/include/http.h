#define HTTP_HEADER_LEN 1024

void *handle_http_request(void *arg);
void *handle_https_request(void *arg);
void *HTTP_SERVER(void *arg);
void *HTTPS_SERVER(void *arg);