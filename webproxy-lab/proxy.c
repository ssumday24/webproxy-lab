/*
1. cd webproxy-lab 
2. make clean && make
3. ./proxy 8080

cd webproxy-lab && ./proxy 8080
http://localhost:8080/home.html
*/

#include "csapp.h"
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* 삭제x - 프록시가 백엔드 서버로 보낼 User-Agent 헤더 */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";


void doit(int fd); // HTTP 트랜잭션 처리
void parse_requesthdrs(rio_t *rp, char *host_hdr, char *other_hdrs); // 요청 헤더 파싱
int parse_uri(char *uri, char *hostname, char *port, char *path, const char *client_host_hdr);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg); // 클라이언트 오류처리

int main(int argc, char **argv) {
    int listenfd; // 리스닝 소켓
    int connfd;   // 연결 소켓  (클라이언트와의 연결)
    char hostname[MAXLINE], port[MAXLINE]; // 클라이언트 호스트 이름 및 포트
    socklen_t clientlen; // 클라이언트 주소 구조체 크기
    struct sockaddr_storage clientaddr; // 클라이언트 소켓 주소 구조체

    // 예외처리 - 입력 예시) ./proxy 80
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    // 주어진 포트로 리스닝 소켓 열기 - argv[1]은 포트 번호
    listenfd = Open_listenfd(argv[1]); // 이 헬퍼함수에 socket()->bind()->listen() 포함


    while (1) {
        clientlen = sizeof(clientaddr); // 클라이언트 주소 구조체 크기 초기화


        // Accept 는 새로운 연결 소켓fd 반환
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);

        // 클라이언트의 호스트 이름과 포트 정보를 가져와 출력
        Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);

        // 이 프록시는 순차적으로 요청을 처리 (하나의 doit 호출이 끝나야 다음 Accept가 진행).
        doit(connfd);

        //연결 소켓 닫기
        Close(connfd);
    }

    return 0;
}

// doit - 프록시 역할 - 응답을 클라이언트에 전달
void doit(int fd) {
    char buf[MAXLINE];
    char method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char hostname[MAXLINE], port[MAXLINE], path[MAXLINE];
    char host_hdr[MAXLINE], other_hdrs[MAXBUF]; // 클라이언트 요청 헤더 저장용
    rio_t rio;
    rio_t server_rio;
    int server_fd_valid = 0;
    ssize_t n;
    int content_length = -1;

    Rio_readinitb(&rio, fd);

    // 1. 클라이언트의 요청 라인 읽기
    if (Rio_readlineb(&rio, buf, MAXLINE) == 0) {
        return;
    }
    printf("Request line: %s", buf);
    sscanf(buf, "%s %s %s", method, uri, version);

    if (strcasecmp(method, "GET") != 0) {
        clienterror(fd, method, "501", "Not Implemented",
                    "Proxy does not implement this method");
        return;
    }

    // 3. 클라이언트 요청 헤더 파싱 (Host 헤더를 먼저 얻기 위함)
    parse_requesthdrs(&rio, host_hdr, other_hdrs);

    // 2. URI 파싱: 호스트 이름, 포트, 경로 추출 (파싱된 Host 헤더 전달)
    // if (parse_uri(uri, hostname, port, path, host_hdr) < 0) { ... }  // 이 부분 주석처리
    strcpy(hostname, "localhost");
    strcpy(port, "8000");
    strcpy(path, uri);

    int server_fd = Open_clientfd(hostname, port); // ← 이 줄 추가!
    if (server_fd < 0) {
        clienterror(fd, "Proxy", "502", "Bad Gateway", "Failed to connect to end server");
        return;
    }
    server_fd_valid = 1;

    Rio_readinitb(&server_rio, server_fd);

    // 5. 백엔드 서버로 보낼 새로운 HTTP 요청 헤더 구성
    char new_request_hdrs[MAXBUF];
    sprintf(new_request_hdrs, "GET %s HTTP/1.0\r\n", path);
    strcat(new_request_hdrs, "Host: localhost:8000\r\n"); // Host 헤더 고정

    // parse_requesthdrs에서 얻은 Host 헤더를 그대로 사용한다.
    strcat(new_request_hdrs, host_hdr);

    strcat(new_request_hdrs, user_agent_hdr);
    strcat(new_request_hdrs, "Connection: close\r\n");
    strcat(new_request_hdrs, "Proxy-Connection: close\r\n");
    strcat(new_request_hdrs, other_hdrs);
    strcat(new_request_hdrs, "\r\n");

    // 6. 구성된 HTTP 요청을 백엔드 서버로 전송
    Rio_writen(server_fd, new_request_hdrs, strlen(new_request_hdrs));
    printf("--- Request sent to origin server ---\n%s-----------------------------------\n", new_request_hdrs);

    // 1. 헤더 전달 및 Content-Length 파싱
    while ((n = Rio_readlineb(&server_rio, buf, MAXLINE)) != 0) {
        Rio_writen(fd, buf, n);
        if (strcmp(buf, "\r\n") == 0) break; // 헤더 끝
        if (strncasecmp(buf, "Content-length:", 15) == 0) {
            content_length = atoi(buf + 15);
        }
    }

    // 2. 본문 전달
    if (content_length > 0) {
        int left = content_length;
        while (left > 0) {
            n = Rio_readnb(&server_rio, buf, left < MAXBUF ? left : MAXBUF);
            if (n <= 0) break;
            Rio_writen(fd, buf, n);
            left -= n;
        }
    } else {
        // Content-Length가 없으면 EOF까지 읽어서 전달
        while ((n = Rio_readnb(&server_rio, buf, MAXBUF)) > 0) {
            Rio_writen(fd, buf, n);
        }
    }
    printf("-----------------------------------\n");

    // 8. 백엔드 서버와의 연결 소켓을 닫는다.
    if (server_fd_valid) {
        Close(server_fd);
    }
}

/*
 * parse_requesthdrs - 클라이언트 요청의 헤더들을 파싱하여 Host 헤더와 나머지 헤더들을 분리한다.
 */
void parse_requesthdrs(rio_t *rp, char *host_hdr, char *other_hdrs) {
    char buf[MAXLINE];
    host_hdr[0] = '\0';   // Host 헤더 버퍼 초기화
    other_hdrs[0] = '\0'; // 기타 헤더 버퍼 초기화

    // 빈 줄이 나올 때까지 헤더 라인을 읽습니다.
    while (Rio_readlineb(rp, buf, MAXLINE) > 0 && strcmp(buf, "\r\n")) {
        // Host 헤더 찾기 (대소문자 구분 없이)
        if (strncasecmp(buf, "Host:", 5) == 0) {
            strcpy(host_hdr, buf); // Host 헤더를 별도로 저장
        }
        // Connection, Proxy-Connection, User-Agent 헤더는 프록시가 재정의하므로 무시합니다.
        else if (strncasecmp(buf, "Connection:", 11) == 0) {
            continue;
        } else if (strncasecmp(buf, "Proxy-Connection:", 17) == 0) {
            continue;
        } else if (strncasecmp(buf, "User-Agent:", 11) == 0) {
            continue;
        }
        // 그 외의 모든 헤더는 'other_hdrs'에 추가합니다.
        else {
            strcat(other_hdrs, buf);
        }
    }
    return;
}

/*
 * parse_uri - URI를 호스트 이름, 포트, 경로로 분리합니다.
 * 예: http://hostname:port/path -> hostname, port, path
 * http://hostname/path -> hostname, "80", path
 * http://hostname -> hostname, "80", "/"
 */
 int parse_uri(char *uri, char *hostname, char *port, char *path, const char *client_host_hdr) {
    char *host_start = strstr(uri, "//");
    char temp_hostname[MAXLINE]; // 임시 저장용
    char temp_port[MAXLINE];     // 임시 저장용
    char *remainder;

    // 1. 절대 URI인 경우 (http:// 로 시작)
    if (host_start != NULL) {
        host_start += 2; // "//" 다음으로 이동
        remainder = host_start;

        char *port_start = strstr(remainder, ":");
        char *path_start = strstr(remainder, "/");

        // 호스트명 추출
        if (port_start != NULL && (path_start == NULL || port_start < path_start)) {
            // 포트가 명시되어 있고, 경로가 없거나 포트보다 뒤에 있는 경우
            strncpy(temp_hostname, remainder, port_start - remainder);
            temp_hostname[port_start - remainder] = '\0';
            remainder = port_start + 1; // ':' 다음으로 이동

            // 포트 추출
            if (path_start != NULL && port_start < path_start) {
                strncpy(temp_port, remainder, path_start - remainder);
                temp_port[path_start - remainder] = '\0';
                strcpy(path, path_start);
            } else { // 포트만 있고 경로가 없는 경우 (예: http://host:port)
                strcpy(temp_port, remainder);
                strcpy(path, "/");
            }
        } else if (path_start != NULL) {
            // 경로만 있고 포트가 명시되지 않은 경우 (예: http://host/path)
            strncpy(temp_hostname, remainder, path_start - remainder);
            temp_hostname[path_start - remainder] = '\0';
            strcpy(temp_port, "80"); // 기본 HTTP 포트
            strcpy(path, path_start);
        } else {
            // 호스트명만 있는 경우 (예: http://host)
            strcpy(temp_hostname, remainder);
            strcpy(temp_port, "80"); // 기본 HTTP 포트
            strcpy(path, "/"); // 기본 경로
        }
        strcpy(hostname, temp_hostname);
        strcpy(port, temp_port);
    }
    // 2. 상대 경로 URI인 경우 (Host 헤더에 의존)
    else {
        // URI 자체가 경로가 된다.
        strcpy(path, uri);
        if (path[0] == '\0') { // URI가 빈 문자열이면 '/'로 설정
            strcpy(path, "/");
        }

        // client_host_hdr (예: "Host: example.com:8080\r\n")에서 호스트명과 포트 파싱
        if (strlen(client_host_hdr) == 0) {
            // Host 헤더가 없는 경우, 프록시로서는 대상 서버를 알 수 없어 오류이다.
            return -1;
        }

        char *host_hdr_value_start = strstr(client_host_hdr, "Host:");
        if (host_hdr_value_start == NULL) return -1; // Host: 가 없으면 오류 (strlen > 0인데도)
        host_hdr_value_start += 5; // "Host:" 다음으로 이동

        // 앞에 있는 공백 건너뛰기
        while (*host_hdr_value_start == ' ' || *host_hdr_value_start == '\t') {
            host_hdr_value_start++;
        }

        // 줄의 끝 (\r 또는 \n)을 찾아서 Host 헤더 값만 추출
        char *host_end = strchr(host_hdr_value_start, '\r');
        if (host_end == NULL) host_end = strchr(host_hdr_value_start, '\n');
        if (host_end == NULL) host_end = host_hdr_value_start + strlen(host_hdr_value_start);

        // 파싱을 위한 임시 문자열 생성 (Host: 값만)
        char host_port_str[MAXLINE];
        strncpy(host_port_str, host_hdr_value_start, host_end - host_hdr_value_start);
        host_port_str[host_end - host_hdr_value_start] = '\0';

        // 호스트명과 포트 분리 (콜론 여부 확인)
        char *colon = strchr(host_port_str, ':');
        if (colon != NULL) {
            // 콜론이 있으면 호스트명과 포트 분리
            *colon = '\0'; // 호스트명 문자열 끝을 널 종료
            strcpy(temp_hostname, host_port_str);
            strcpy(temp_port, colon + 1); // 콜론 다음부터 포트
        } else {
            // 콜론이 없으면 포트는 기본 80으로 설정
            strcpy(temp_hostname, host_port_str);
            strcpy(temp_port, "80");
        }
        strcpy(hostname, temp_hostname);
        strcpy(port, temp_port);
    }
    return 0; // 성공
}
/*
 * clienterror - 클라이언트에 HTTP 오류 메시지를 보냅니다.
 */
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
    char buf[MAXLINE]; // HTTP 헤더 버퍼
    char body[MAXBUF]; // HTTP 응답 본문 버퍼

    // HTTP 응답 본문 (HTML 형식) 생성
    // sprintf를 사용하여 초기화
    sprintf(body, "<html><title>Proxy Error</title>\r\n");
    // 이후부터는 strcat을 사용하여 안전하게 문자열을 이어붙입니다.
    strcat(body, "<body bgcolor=\"ffffff\">\r\n");

    // 오류 메시지 및 원인을 body에 추가하기 위해 임시 버퍼 사용
    sprintf(buf, "%s: %s\r\n", errnum, shortmsg);
    strcat(body, buf);
    sprintf(buf, "<p>%s: %s\r\n", longmsg, cause);
    strcat(body, buf);

    strcat(body, "<hr><em>The Proxy server</em>\r\n");
    strcat(body, "</body></html>\r\n");


    // HTTP 응답 헤더 생성 및 전송
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg); // 상태 라인 (예: HTTP/1.0 404 Not Found)
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n"); // Content-Type 헤더
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body)); // Content-Length 및 헤더 종료
    Rio_writen(fd, buf, strlen(buf));

    // 응답 본문 전송
    Rio_writen(fd, body, strlen(body));
}