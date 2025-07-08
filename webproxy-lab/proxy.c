#include "csapp.h" // CSAPP 래퍼 함수들을 포함합니다.

/* 권장 캐시 및 객체 최대 크기 (이 코드에서는 캐싱 미구현) */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* 프록시가 백엔드 서버로 보낼 User-Agent 헤더 */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

/* 함수 프로토타입 선언 */
void doit(int fd); // 실제 HTTP 트랜잭션을 처리하는 함수
void parse_requesthdrs(rio_t *rp, char *host_hdr, char *other_hdrs); // 요청 헤더를 파싱하는 함수
int parse_uri(char *uri, char *hostname, char *port, char *path); // URI를 파싱하는 함수
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg); // 클라이언트 오류 응답을 생성하는 함수

int main(int argc, char **argv) {
    int listenfd; // 리스닝 소켓 디스크립터
    int connfd;   // 연결 소켓 디스크립터 (클라이언트와의 연결)
    char hostname[MAXLINE], port[MAXLINE]; // 클라이언트 호스트 이름 및 포트
    socklen_t clientlen; // 클라이언트 주소 구조체 크기
    struct sockaddr_storage clientaddr; // 클라이언트 소켓 주소 구조체

    // 명령줄 인자 개수 확인: `usage: ./proxy <port>`
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    // 주어진 포트로 리스닝 소켓을 엽니다.
    // argv[1]은 프록시가 클라이언트로부터 연결을 기다릴 포트 번호입니다.
    listenfd = Open_listenfd(argv[1]);

    // 무한 루프: 클라이언트 연결을 계속해서 받아들입니다.
    while (1) {
        clientlen = sizeof(clientaddr); // 클라이언트 주소 구조체 크기 초기화
        // 클라이언트 연결 요청을 받아들입니다.
        // Accept 함수는 새로운 연결 소켓 디스크립터(connfd)를 반환합니다.
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);

        // 클라이언트의 호스트 이름과 포트 정보를 가져와 출력합니다.
        Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);

        // 실제 HTTP 트랜잭션 처리를 doit 함수에 위임합니다.
        // 이 프록시는 순차적으로 요청을 처리합니다 (하나의 doit 호출이 끝나야 다음 Accept가 진행).
        doit(connfd);

        // 트랜잭션 처리가 끝나면 클라이언트와의 연결 소켓을 닫습니다.
        Close(connfd);
    }
    // 이 코드는 무한 루프이므로 여기에 도달하지 않습니다.
    return 0;
}

/*
 * doit - 클라이언트 요청을 처리하고 서버 응답을 클라이언트에 전달합니다.
 */
void doit(int fd) {
    char buf[MAXLINE]; // 임시 버퍼
    char method[MAXLINE], uri[MAXLINE], version[MAXLINE]; // 요청 라인 파싱용
    char hostname[MAXLINE], port[MAXLINE], path[MAXLINE]; // URI 파싱용
    char host_hdr[MAXLINE], other_hdrs[MAXBUF]; // 클라이언트 요청 헤더 저장용
    rio_t rio; // 클라이언트와의 통신을 위한 Rio 버퍼
    rio_t server_rio; // 백엔드 서버와의 통신을 위한 Rio 버퍼
    int server_fd_valid = 0; // server_fd가 유효하게 열렸는지 추적

    // 클라이언트 소켓 디스크립터에 Rio 버퍼 초기화
    Rio_readinitb(&rio, fd);

    // 1. 클라이언트의 요청 라인 읽기 (예: GET http://www.example.com/index.html HTTP/1.0)
    if (Rio_readlineb(&rio, buf, MAXLINE) == 0) { // EOF 또는 읽기 오류 시
        return;
    }
    printf("Request line: %s", buf); // 요청 라인 출력 (디버깅용)
    sscanf(buf, "%s %s %s", method, uri, version); // 메소드, URI, 버전 파싱

    // GET 메소드만 지원
    if (strcasecmp(method, "GET") != 0) {
        clienterror(fd, method, "501", "Not Implemented",
                    "Proxy does not implement this method");
        return;
    }

    // 2. URI 파싱: 호스트 이름, 포트, 경로 추출
    if (parse_uri(uri, hostname, port, path) < 0) {
        clienterror(fd, uri, "400", "Bad Request", "URI could not be parsed");
        return;
    }
    printf("Parsed URI: Host=%s, Port=%s, Path=%s\n", hostname, port, path); // 파싱 결과 출력

    // 3. 클라이언트 요청 헤더 파싱
    // `Host` 헤더와 다른 헤더들을 분리하여 저장합니다.
    parse_requesthdrs(&rio, host_hdr, other_hdrs);

    // 4. 백엔드 서버에 연결
    int server_fd = Open_clientfd(hostname, port);
    if (server_fd < 0) {
        clienterror(fd, "Proxy", "502", "Bad Gateway", "Failed to connect to end server");
        return;
    }
    server_fd_valid = 1; // 서버 소켓이 성공적으로 열렸음을 표시

    // 백엔드 서버 소켓 디스크립터에 Rio 버퍼 초기화
    Rio_readinitb(&server_rio, server_fd);

    // 5. 백엔드 서버로 보낼 새로운 HTTP 요청 헤더 구성
    char new_request_hdrs[MAXBUF];
    // 요청 라인 (GET /path HTTP/1.0)
    sprintf(new_request_hdrs, "GET %s HTTP/1.0\r\n", path);

    // Host 헤더 처리 (클라이언트가 Host 헤더를 보냈다면 그것을 사용, 아니면 URI 파싱 결과로 생성)
    if (strlen(host_hdr) == 0) { // 클라이언트가 Host 헤더를 보내지 않은 경우
        if (strcmp(port, "80") == 0) { // 기본 HTTP 포트 80인 경우 포트 생략
            sprintf(host_hdr, "Host: %s\r\n", hostname);
        } else {
            sprintf(host_hdr, "Host: %s:%s\r\n", hostname, port);
        }
    }
    strcat(new_request_hdrs, host_hdr);

    // User-Agent 헤더 추가 (프록시의 User-Agent 사용)
    strcat(new_request_hdrs, user_agent_hdr);

    // Connection: close 헤더 추가 (지속적인 연결 사용 안 함)
    strcat(new_request_hdrs, "Connection: close\r\n");

    // Proxy-Connection: close 헤더 추가 (프록시 연결 닫기 명시)
    strcat(new_request_hdrs, "Proxy-Connection: close\r\n");

    // 클라이언트가 보낸 다른 모든 헤더들을 추가합니다.
    strcat(new_request_hdrs, other_hdrs);

    // 요청 헤더의 끝을 나타내는 빈 줄
    strcat(new_request_hdrs, "\r\n");

    // 6. 구성된 HTTP 요청을 백엔드 서버로 전송
    Rio_writen(server_fd, new_request_hdrs, strlen(new_request_hdrs));
    printf("--- Request sent to origin server ---\n%s-----------------------------------\n", new_request_hdrs); // 디버깅용

    // 7. 백엔드 서버로부터 응답을 받아서 클라이언트에 전달
    ssize_t n;
    char server_response_buf[MAXLINE];
    printf("--- Response from origin server ---\n"); // 디버깅용
    while ((n = Rio_readlineb(&server_rio, server_response_buf, MAXLINE)) != 0) {
        printf("%s", server_response_buf); // 응답 내용 출력 (디버깅용)
        Rio_writen(fd, server_response_buf, n); // 클라이언트에게 응답 전달
    }
    printf("-----------------------------------\n");

    // 8. 백엔드 서버와의 연결 소켓을 닫습니다.
    if (server_fd_valid) {
        Close(server_fd);
    }
}

/*
 * parse_requesthdrs - 클라이언트 요청의 헤더들을 파싱하여 Host 헤더와 나머지 헤더들을 분리합니다.
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
int parse_uri(char *uri, char *hostname, char *port, char *path) {
    char *host_start = strstr(uri, "//"); // "http://" 다음의 호스트 시작 지점 찾기
    if (host_start == NULL) {
        // "http://" 접두사가 없는 경우, 현재 프록시는 처리하지 않습니다.
        // (더 복잡한 프록시는 이 경우에도 호스트를 유추하거나 오류를 반환합니다.)
        return -1; // 유효하지 않은 URI
    }
    host_start += 2; // "//" 다음으로 포인터 이동

    char *port_start = strstr(host_start, ":"); // 포트 시작 지점 찾기
    char *path_start = strstr(host_start, "/"); // 경로 시작 지점 찾기

    // 1. URI에 경로가 없는 경우 (예: "http://example.com")
    if (path_start == NULL) {
        strcpy(path, "/"); // 경로를 "/"로 설정합니다.
        if (port_start != NULL) { // 포트가 있는 경우 (예: "http://example.com:8080")
            strncpy(hostname, host_start, port_start - host_start);
            hostname[port_start - host_start] = '\0'; // null 종료
            strcpy(port, port_start + 1); // 포트 문자열 복사
        } else { // 포트가 없는 경우 (예: "http://example.com")
            strcpy(hostname, host_start); // 전체를 호스트 이름으로 복사
            strcpy(port, "80"); // 기본 HTTP 포트 80 설정
        }
        return 0; // 성공
    }

    // 2. URI에 경로가 있는 경우
    if (port_start != NULL && port_start < path_start) {
        // 호스트명 추출 (콜론 앞까지)
        strncpy(hostname, host_start, port_start - host_start);
        hostname[port_start - host_start] = '\0'; // null 종료
        // 포트 추출 (콜론 뒤부터 경로 앞까지)
        strncpy(port, port_start + 1, path_start - (port_start + 1));
        port[path_start - (port_start + 1)] = '\0'; // null 종료
    } else {
        // 호스트명 추출 (경로 앞까지, 포트 없음)
        strncpy(hostname, host_start, path_start - host_start);
        hostname[path_start - host_start] = '\0'; // null 종료
        strcpy(port, "80"); // 기본 포트 80 설정
    }

    // 경로 추출
    strcpy(path, path_start);

    return 0; // 성공
}

/*
 * clienterror - 클라이언트에 HTTP 오류 메시지를 보냅니다.
 */
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
    char buf[MAXLINE]; // HTTP 헤더 버퍼
    char body[MAXBUF]; // HTTP 응답 본문 버퍼

    // HTTP 응답 본문 (HTML 형식) 생성
    sprintf(body, "<html><title>Proxy Error</title>\r\n");
    sprintf(body, "%s<body bgcolor=\"ffffff\">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Proxy server</em>\r\n", body);
    sprintf(body, "%s</body></html>\r\n", body);

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