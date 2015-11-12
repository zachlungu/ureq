/*
https://github.com/solusipse/ureq

The MIT License (MIT)

Copyright (c) 2015 solusipse

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#ifndef UREQ_IMPL_H
#define UREQ_IMPL_H

#include "ureq_fwd.h"
#include "ureq_pages.h"
#include "ureq_env_dep.h"

#ifdef UREQ_ESP8266
    #define UREQ_STATIC_LIST
#endif

#ifdef UREQ_STATIC_LIST
    static UreqPage pages[16];
    static int pageCount = 0;
#else
    static UreqPage *pages = NULL;
    static int pageCount = 0;
#endif

#ifdef UREQ_ESP8266
    #include "../hardware/ureq_esp8266.h"
#endif

#ifdef UREQ_USE_FILESYSTEM
    #include "ureq_filesystem.h"
#endif

const char *ureq_methods[] = {
    UREQ_GET,
    UREQ_POST,
    UREQ_ALL,
    UREQ_PUT,
    UREQ_DELETE,
    NULL
};

const UreqMime ureq_mime_types[] = {
    /* Text mimes */
    {"html",    "text/html"},
    {"htm",     "text/html"},
    {"js",      "text/javascript"},
    {"txt",     "text/plain"},
    {"css",     "text/css"},
    {"xml",     "text/xml"},
    
    /* Image mimes */
    {"bmp",     "image/bmp"},
    {"gif",     "image/gif"},
    {"png",     "image/png"},
    {"jpg",     "image/jpeg"},
    {"jpeg",    "image/jpeg"},

    /* Application mimes */
    {"json",    "application/json"},

    /*
       The default mime-type is text/html (urls without extensions)
       Use it for files with unknown extensions
    */
    {NULL,      "text/html"}
};

static int ureq_get_header(char *h, const char *r) {
    const char *p = strstr(r, UREQ_EOL);
    if (!p) return 0;
    strncpy(h, r, (p-r));
    return 1;
}

static int ureq_check_method_validity(const char *m) {
    int i;
    for(i = 0; ureq_methods[i] != NULL; ++i)
        if (strcmp(ureq_methods[i], m) == 0)
            return 1;
    return 0;
}

static int ureq_parse_header(HttpRequest *req, const char *r) {

    const size_t r_len = strlen(r) + 1;

    char *header = malloc(r_len);
    char *b = NULL;
    char *bk = NULL;

    if (!ureq_get_header(header, r)) {
        free(header);
        return 0;
    }

    b = strtok_r(header, " ", &bk);
    if (!ureq_check_method_validity(b)) {
        free(header);
        return 0;
    }
    req->type = malloc(strlen(b) + 1);
    strcpy(req->type, b);

    b = strtok_r(NULL, " ", &bk);
    if (!b) {
        free(header);
        return 0;
    }
    req->url = malloc(strlen(b) + 1);
    strcpy(req->url, b);

    b = strtok_r(NULL, " ", &bk);
    if (!b) {
        free(header);
        return 0;
    }
    if (strncmp(b, "HTTP/1.", 7)) {
        free(header);
        return 0;
    }
    req->version = malloc(strlen(b) + 1);
    strcpy(req->version, b);
    free(header);

    req->message = malloc(r_len);
    strcpy(req->message, r);

    req->params             = NULL;
    req->response.data      = NULL;
    req->body               = NULL;
    req->response.header    = NULL;
    req->response.mime      = NULL;
    req->response.file      = NULL;
    req->response.code      = 0;

    return 1;
}

void ureq_serve(char *url, char *(*func)(HttpRequest*), char *method) {
    UreqPage page = {url, method, func};

    #ifdef UREQ_STATIC_LIST
        pages[pageCount++] = page;
    #else
        pages = (UreqPage*) realloc(pages, ++pageCount * sizeof(UreqPage));
        pages[pageCount-1] = page;
    #endif
}

HttpRequest ureq_init(const char *ur) {
    HttpRequest r = {};

    r.complete = -1;

    /* These basic checks protect against buffer overflow */
    if (strlen(ur) > UREQ_BUFFER_SIZE) {
        r.response.code  = 413;
        r.valid = 0;
        return r;
    }

    if (strlen(ur) < UREQ_HTTP_REQ_LEN) {
        r.response.code = 400;
        r.valid = 0;
        return r;
    }

    if(!strstr(ur, "HTTP/1.")) {
        r.response.code = 400;
        r.valid = 0;
        return r;
    }

    char bh[16];
    strncpy(bh, ur, 16);

    int i, v=0;
    for(i = 0; ureq_methods[i] != NULL; ++i)
        if (strstr(bh, ureq_methods[i])) {
            v=1;
            break;
        }

    if (!v) {
        r.response.code = 400;
        r.valid = 0;
        return r;
    }

    /* Actual parsing */
    if (!ureq_parse_header(&r, ur))
        r.valid = 0;
    else
        r.valid = 1;

    return r;
}

static int ureq_first_run(HttpRequest *req) {
    req->complete = -2;

    int i;
    for (i = 0; i < pageCount; ++i) {
        /*
           This loop iterates through a pages list and compares
           urls and methods to requested ones. If there's a match,
           it calls a corresponding function and saves http
           response to req.response.
        */

        /* Start from checking if page's special (e.g. 404) */
        if (!req->page404)
            if (!strcmp(pages[i].url, "404"))
                req->page404 = pages[i].func;

        char *plain_url = malloc(strlen(req->url) + 1);
        ureq_remove_parameters(plain_url, req->url);

        /* If there's no match between this, skip to next iteration. */
        if (strcmp(plain_url, pages[i].url)) {
            free(plain_url);
            continue;
        }
        free(plain_url);

        /*
           If request type is ALL, corresponding function is always called,
           no matter which method type was used.
        */
        if (strcmp(UREQ_ALL, pages[i].method) != 0) {
            /*
               If there's no match between an url and method, skip
               to next iteration.
            */
            if (strcmp(req->type, pages[i].method)) {
                continue;
            }
        }

        /* Save get parameters to r->params */
        ureq_get_query(req);

        /* If method was POST, save body to r->message */
        if (!strcmp(UREQ_POST, req->type))
            ureq_set_post_data(req);

        /*
           Run page function but don't save data from it
           at first run (use it now only to set some things).
           If user returns blank string, don't send data again
           (complete code 2)
        */
        if (!strlen(pages[i].func(req)))
            req->complete = 2;

        /* Save pointer to page's func for later use */
        req->func = pages[i].func;

        /*
           If user decided to bind an url to file, set mimetype
           here and break.
        */
        if (req->response.file) {
            if (!req->response.mime )
                req->response.mime = ureq_set_mimetype(req->response.file);
            break;
        }

        if (req->response.code == 0)
            req->response.code = 200;

        /* Return only header at first run */
        req->response.data = ureq_generate_response_header(req);
        req->len = strlen(req->response.data);

        return req->complete;
    }
    #ifdef UREQ_USE_FILESYSTEM
        return ureq_fs_first_run(req);
    #else
        return ureq_set_404_response(req);
    #endif
}

static void ureq_parse_template(char *dst, char *buf, char *from, char *to) {
    // TODO: iterate through buffer with {{ and }}, replace inside for loop
    // (see render template method)
    int s = strlen(dst);
    if (!(dst = strstr(dst, from))) return;
    dst[0] = 0;
    memset(buf, 0, s);
    strcat(buf, dst);
    strcat(buf, to);
    strcat(buf, dst+strlen(from));

    strcpy(dst, buf);

    ureq_parse_template(dst,buf,from,to);
}

static void ureq_render_template(HttpRequest *r) {
    if (r->tmp_len <= 0) return;
    int i, tlen;
    for (i=0,tlen=0; i < r->tmp_len; i++) {
        tlen += strlen(r->templates[i].value);
        tlen -= strlen(r->templates[i].destination);
        tlen -= 4; /* special characers - {{x}} */
    }
    if (tlen > 0) {
    // if this is true, it means that rendered page
    // will have more characters than the template
        if (tlen > UREQ_BUFFER_SIZE) {
        // This template cannot be rendered
        // without buffer overflow
            // TODO: return 500 here
            r->response.data = "Template parsing error: buffer overflow prevented.";
            return;
        }
        if ((tlen + strlen(r->buffer)) > UREQ_BUFFER_SIZE) {
        // If piece of file being rendered is bigger than
        // the buffer, some operations have to be performed
       
        // TODO: this will crash when template variable is exactly
        //       at the seam (between buffer iterations).
        // TODO: handle special chars, eg. {{ }}

            if (r->big_file) {
                r->buffer[strlen(r->buffer)-tlen] = 0;
                r->file.address -= tlen;
                r->file.size += tlen;
            } else {
                r->complete -= 1;
            }
        }
    }
    // Everything's prepared for running replacing function
    for (i=0; i < r->tmp_len; i++) {
        ureq_parse_template(r->buffer, r->_buffer, r->templates[i].destination, r->templates[i].value);
    }

    if (r->len != UREQ_BUFFER_SIZE) r->len = strlen(r->buffer);
}

static int ureq_next_run(HttpRequest *req) {
    if (req->complete == -2) {
        free(req->response.data);
    }

    UreqResponse respcpy = req->response;
    req->complete--;

    if (req->big_file) {
        #if defined UREQ_USE_FILESYSTEM
            if (req->file.size > UREQ_BUFFER_SIZE) {
                respcpy.data = ureq_fs_read(req->file.address, UREQ_BUFFER_SIZE, req->buffer);
                req->file.address += UREQ_BUFFER_SIZE;
                req->file.size -= UREQ_BUFFER_SIZE;
                req->len = UREQ_BUFFER_SIZE;
                ureq_render_template(req);
                req->complete -= 1;
            } else {
                req->len = req->file.size;
                respcpy.data = ureq_fs_read(req->file.address, req->file.size, req->buffer);
                ureq_render_template(req);
                req->complete = 1;
            }
        #else
            // TODO: buffer read from func
            respcpy.data = req->func(req);
            req->len = strlen(respcpy.data);
        #endif
    } else {
        respcpy.data = req->func(req);
        req->len = strlen(respcpy.data);
        req->complete = 1;
    }
    req->response = respcpy;
    return req->complete;

}

static int ureq_set_404_response(HttpRequest *r) {
    char *page;
    r->response.code = 404;

    if (r->page404)
        page = r->page404(r);
    else
        page = (char *) UREQ_HTML_PAGE_404;
    
    ureq_generate_response(r, page);
    return r->complete = 1;
}

static char *ureq_get_error_page(HttpRequest *r) {
    const char *desc = ureq_get_code_description(r->response.code);
    sprintf(r->buffer, "%s%d %s%s%d %s%s", \
            UREQ_HTML_HEADER, r->response.code, desc, \
            UREQ_HTML_BODY, r->response.code, desc, \
            UREQ_HTML_FOOTER);
    return r->buffer;
}

static int ureq_set_error_response(HttpRequest *r) {
    if (!r->response.code) r->response.code = 400;
    r->response.mime = "text/html";
    r->response.header = "";
    ureq_generate_response(r, ureq_get_error_page(r));
    return r->complete = 1;
}

int ureq_run(HttpRequest *req) {
    /*
       Code meanings:
        1: Everything went smooth

        2: User provided blank string, don't send data
           for the second time (and free header)

      <-1: Still running, on -2 free header which is
           dynamically alloced
    */
    if (req->complete == 1)
        return 0;

    if (req->complete == 2) {
        free(req->response.data);
        return 0;
    }

    /*
       If code equals to -1, it's the very first run,
       parameters are set there and header is sent.

       Data (if any), will be sent in next run(s).
    */
    if (req->complete == -1) {
        /* If request was invalid, set everything to null */
        if (!req->valid) return ureq_set_error_response(req);
        return ureq_first_run(req);
    }
    
    if ((req->complete < -1) && (req->response.code != 404))
        return ureq_next_run(req);

    return 0;
}

static void ureq_generate_response(HttpRequest *r, char *html) {
    char *header = ureq_generate_response_header(r);
    r->response.data = malloc(strlen(header) + strlen(html) + 3);

    strcpy(r->response.data, header);
    strcat(r->response.data, html);
    strncat(r->response.data, UREQ_EOL, UREQ_EOL_LEN);

    r->len = strlen(r->response.data);

    free(header);
}

static const char *ureq_get_code_description(const int c) {
    switch (c) {
        case 200: return "OK";
        case 302: return "Found";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 408: return "Request Timeout";
        case 413: return "Request-URI Too Long";
        case 500: return "Internal Error";
        case 503: return "Service Temporarily Overloaded";
        default:  return "Not Implemented";
    }
}

static char *ureq_set_mimetype(const char *r) {
    const char *e = strrchr(r, '.');
    if (!e) return "text/html";
    e += 1;

    int i;
    for (i=0; ureq_mime_types[i].ext != NULL; ++i)
        if (!strcmp(ureq_mime_types[i].ext, e)) break;

    return (char*) ureq_mime_types[i].mime;
}

static char *ureq_generate_response_header(HttpRequest *r) {
    /* Set default mime type if blank */
    if (!r->response.mime) {
        if (r->response.code == 200 || r->response.code == 404) {
            r->response.mime = ureq_set_mimetype(r->url);
        } else {
            r->response.mime = "";
        }
    } 

    char *br = malloc(strlen(r->response.mime) + 15);
    strcpy(br, "Content-Type: ");
    strcat(br, r->response.mime);

    if (r->response.header) {
        char *bb = malloc(strlen(r->response.header) + 1);
        strcpy(bb, r->response.header);
        r->response.header = malloc(strlen(br) + strlen(bb) + UREQ_EOL_LEN + 1);
        strcpy(r->response.header, br);
        strcat(r->response.header, UREQ_EOL);
        strcat(r->response.header, bb);
        free(bb);
    } else {
        r->response.header = malloc(strlen(br) + UREQ_EOL_LEN + 1);
        strcpy(r->response.header, br);
        strcat(r->response.header, UREQ_EOL);
    }

    free(br);

    const char *desc = ureq_get_code_description(r->response.code);

    size_t hlen = strlen(UREQ_HTTP_V) + 4 /*response code*/ + strlen(desc) + \
                  strlen(r->response.header) + 8/*spaces,specialchars*/;

    char *h = malloc(hlen + 1);
    sprintf(h, "%s %d %s\r\n%s\r\n", UREQ_HTTP_V, r->response.code, desc, r->response.header);
    
    return h;
}

static void ureq_set_post_data(HttpRequest *r) {
    char *n = strstr(r->message, "\r\n\r\n");
    if (!n) return;
    r->body = n + 4;
}

static void ureq_param_to_value(char *data, char *buffer, const char *arg) {
    char *bk, *buf;
    for (buf = strtok_r(data, "&", &bk); buf != NULL; buf = strtok_r(NULL, "&", &bk)) {

        if (strstr(buf, arg) == NULL) continue;

        char *sptr;
        buf = strtok_r(buf, "=", &sptr);

        if (strcmp(buf, arg) == 0) {
            strcpy(buffer, sptr);
            return;
        }
    }
    *buffer = '\0';
}

char *ureq_get_param_value(HttpRequest *r, const char *arg) {
    if (!r->params) return "\0";
    char *data = malloc(strlen(r->params) + 1);
    strcpy(data, r->params);
    ureq_param_to_value(data, r->_buffer, arg);
    free(data);
    return r->_buffer;
}

char *ureq_post_param_value(HttpRequest *r, const char *arg) {
    if (!r->body) return "\0";
    char *data = malloc(strlen(r->body) + 1);
    strcpy(data, r->body);
    ureq_param_to_value(data, r->_buffer, arg);
    free(data);
    return r->_buffer;
}

static void ureq_remove_parameters(char *b, const char *u) {
    char *bk;
    strcpy(b, u);
    b = strtok_r(b, "?", &bk);
}

static void ureq_get_query(HttpRequest *r) {
    char *q = strchr(r->url, '?');
    if (!q) return;
    r->params = q + 1;
}

void ureq_template(HttpRequest *r, char *d, char *v) {
    if (r->complete != -2) return;
    UreqTemplate t = {d, v};
    r->templates[r->tmp_len++] = t;
}

void ureq_close(HttpRequest *req) {
    if (req->type)      free(req->type);
    if (req->url)       free(req->url);
    if (req->version)   free(req->version);
    if (req->message)   free(req->message);

    if (!req->valid || req->response.code == 404)
        free(req->response.data);

    if (req->response.header)
        if (strlen(req->response.header) > 1)
            free(req->response.header);
}

void ureq_finish() {
    #ifndef UREQ_STATIC_LIST
        free(pages);
    #endif
}

#endif /* UREQ_IMPL_H */