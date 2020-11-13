#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "query/query.h"
#include "query/query_codes.h"
#include "config.h"

#define PARSER static inline
#define unused __attribute__((unused))

DEFINE_TRIVIAL_LINKED_LIST(fields, char*, field);

struct response {
    int code;
    char* retstring;
    struct fields* fields;
};


#define query_string_parser strtok_r

PARSER struct response query_response_parser(const char* r) {
    struct response response = {0};
    struct fields*  fields   = response.fields = malloc(sizeof *fields);
    char* endptr;
    char* pfield;

    response.code = strtol(r, &endptr, 10);
    if (endptr == r) {
        fprintf(stderr, "query_response_parser: fatal: malformed response \"%s\"\n", r);
        exit(EXIT_FAILURE); 
    }
    
    response.retstring = calloc(1, (pfield = strchr(endptr+1, '\n')) - endptr);
    memcpy(response.retstring, endptr+1, pfield-endptr);
    response.retstring[pfield - endptr - 1] = '\0';

    pfield++;
    while((endptr = strchr(pfield, '|'))) {
        fields->field = calloc(1, endptr - pfield);
        fields->next  = calloc(1, sizeof *fields->next);
        memcpy(fields->field, pfield, endptr - pfield); 
        fields = fields->next;
        pfield = endptr+1;
    }

    fields->field = calloc(1, strlen(pfield));
    memcpy(fields->field, pfield, strlen(pfield)); 
    fields->next = NULL;

    return response;
}

PARSER void query_response_free(struct response* r) {
    free(r->retstring);
    struct fields* fp, *fnext = r->fields;
    while (fnext) {
        fp = fnext;
        fnext = fnext->next;
        free(fp);
    }
    
}

PARSER uint64_t query_int_parser(const char* r unused) {
    return 0;
}
PARSER uint64_t query_int_list_parser(const char* r unused) {
    return 0;
}
PARSER uint64_t query_year_parser(const char* r unused) {
    return 0;
}

QueryObject*    query_new(const char* username, const char* password, const char* client, const char* clientver) {
    QueryObject* qobj = calloc(1, sizeof *qobj);
    query_init(qobj, username, password, client, clientver);
    return qobj; 
}


QueryObject*    query_init(QueryObject* qobj, const char* username, const char* password, const char* client, const char* clientver) {
    qobj->_username  = username;
    qobj->_password  = password;
    qobj->_client    = client;
    qobj->_clientver = clientver;
    return qobj;
}
QueryObject*    query_establish_connection(QueryObject* qobj) {
    struct addrinfo hint = {0}, *results, *result;

    hint.ai_family   = AF_UNSPEC;
    hint.ai_socktype = SOCK_DGRAM;
    hint.ai_flags    = 0;
    hint.ai_protocol = 0;

    qobj->_r = getaddrinfo(ANIDB_ENDPOINT, ANIDB_PORT, &hint, &results);
    if (qobj->_r != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(qobj->_r));
        return NULL;
    }

    for (result = results ; result != NULL ; result = result->ai_next) {
        qobj->_sfd = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
        if (qobj->_sfd == -1) {
            continue;
        }
        if (connect(qobj->_sfd, result->ai_addr, result->ai_addrlen) != -1) {
            break;
        }
        close(qobj->_sfd);
    }
    freeaddrinfo(results);

    if (result == NULL) {
        fprintf(stderr, "query_establish_connection: unable to connect\n");
        return NULL;
    }
    return qobj;
}

const char*     query_refresh_session(QueryObject* qobj) {
    int n;

    n = snprintf(qobj->_buffer, ANIDB_NREAD, ANIDB_AUTHFMT, qobj->_username, qobj->_password, qobj->_clientver, qobj->_client);
    write(qobj->_sfd, qobj->_buffer, n);
    n = read(qobj->_sfd, qobj->_buffer, ANIDB_NREAD);
    qobj->_buffer[n] = '\0';
    
    struct response r = query_response_parser(qobj->_buffer);  
    switch (r.code) {
        case ANIDB_LOGIN_ACCEPTED :
            break;
        case ANIDB_LOGIN_ACCEPTED_NEW_VER_AVL :
            fprintf(stderr, "query_refresh_session: login accepted, new version available\n");
            break;
        case ANIDB_LOGIN_FAILED :
            fprintf(stderr, "query_refresh_session: fatal: login failed\n");
            exit(EXIT_FAILURE);
        case ANIDB_BANNED :
            fprintf(stderr, "query_refresh_session: fatal: banned\n");
            exit(EXIT_FAILURE);
        default :
            fprintf(stderr, "query_refresh_session: fatal: unexpected response code %d\n", r.code);
            exit(EXIT_FAILURE);
    }

    memcpy(qobj->_session, r.retstring, ANIDB_NSESSION);
    qobj->_session[ANIDB_NSESSION] = '\0';

    r.retstring = NULL;

    query_response_free(&r); 

    return qobj->_session;
}

anidb_response  query_by_name(QueryObject* qobj, const char* aname, const char* amask) {
    int n;
retry :
    n = snprintf(qobj->_buffer, ANIDB_NREAD, ANIDB_ANAMEFMT, qobj->_session, aname, amask); 
    write(qobj->_sfd, qobj->_buffer, n);
    n = read(qobj->_sfd, qobj->_buffer, ANIDB_NREAD);
    qobj->_buffer[n] = '\0';
    struct response r = query_response_parser(qobj->_buffer);
    if (r.code == ANIDB_LOGIN_FIRST) {
        query_refresh_session(qobj);
        goto retry;
    }

    return (anidb_response){0};
}
anidb_response  query_by_id(QueryObject* qobj, int aid, const char* amask) {
    int n;
retry:
    n = snprintf(qobj->_buffer, ANIDB_NREAD, ANIDB_ANIIDFMT, qobj->_session, aid, amask); 
    printf("%s\n", qobj->_buffer);
    write(qobj->_sfd, qobj->_buffer, n);
    n = read(qobj->_sfd, qobj->_buffer, ANIDB_NREAD);
    qobj->_buffer[n] = '\0';
    struct response r = query_response_parser(qobj->_buffer);
    if (r.code == ANIDB_LOGIN_FIRST || r.code == ANIDB_INVALID_SESSION) {
        printf("Invalid session or hasn't log in, attempting login\n");
        query_refresh_session(qobj);
        goto retry;
    }
    if (r.code == ANIDB_NO_SUCH_ANIME) {
        fprintf(stderr, "No such anime found\n");
        return (anidb_response){0};
    }

    if (r.code != ANIDB_ANIME) {
        fprintf(stderr, "query_by_id: fatal: unknown return code, received %d\n", r.code);
        exit(EXIT_FAILURE);
    }

#define BIT_DISPATCH(bit,n) 
    printf("%s\n", qobj->_buffer);    

    query_response_free(&r);
    return (anidb_response){0};
}
void            query_free(QueryObject* qobj) {
    close(qobj->_sfd);
}

const char*     query_int_to_amask(uint64_t iamask) {
    static char amask[64];
    snprintf(amask, 64, "%lx", iamask);
    return amask;
}