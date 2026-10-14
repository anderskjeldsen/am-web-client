#define _POSIX_C_SOURCE 200809L
#include <macos-arm/Am/Web/Client/Experimental/HttpReactor.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netdb.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#define SLOTS 8
#define ACTIVE 4
#define CAP (8u*1024u*1024u)
enum { FREE, QUEUED, CONNECTING, HANDSHAKE, SENDING, READING, DONE };
typedef struct {
 int id, phase, fd, tls, cancelled;
 short want;
 int headers_done, framing, reusable, reused, retried, chunk_state, interim;
 size_t header_scan, body_start, expected, chunk_pos, chunk_size;
 struct sockaddr_in address;
 char host[256], request[4096], error[192];
 size_t request_size, sent, used, capacity, limit;
 unsigned char *data;
 SSL *ssl;
 double deadline;
} request;
typedef struct { int fd, tls; unsigned short port; char host[256]; SSL *ssl; double expires; } idle_connection;
typedef struct engine engine;
static pthread_cond_t manager_changed=PTHREAD_COND_INITIALIZER;
static int manager_tick(void);
struct engine {
 pthread_t thread;
 pthread_mutex_t mutex;
 pthread_cond_t changed;
 int wake[2], stop, next_id;
 request slots[SLOTS];
 idle_connection idle[ACTIVE];
 int keepalive;
 size_t response_limit;
};
static double now_ms(void) {
 struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);
 return t.tv_sec*1000.0+t.tv_nsec/1000000.0;
}
static void message(char *out,size_t n,const char *s) { if(n) snprintf(out,n,"%s",s); }
static void wakeup(engine *r) { char b=1; (void)write(r->wake[1],&b,1); }
static request *find(engine *r,int id) {
 for(int i=0;i<SLOTS;i++) if(r->slots[i].phase!=FREE && r->slots[i].id==id) return &r->slots[i];
 return NULL;
}
static void drop_idle(idle_connection *c) {
 if(c->ssl) SSL_free(c->ssl);
 if(c->fd>=0) close(c->fd);
 memset(c,0,sizeof(*c));c->fd=-1;
}
/* Strict bounded HTTP/1 framing: only a completely framed response can be
 * reused. Unknown transfer codings and ambiguous lengths are rejected. */
static int frame(request *q,const char **error) {
 if(!q->headers_done) {
  size_t end=0;
  for(size_t i=q->header_scan;i+3<q->used;i++) if(!memcmp(q->data+i,"\r\n\r\n",4)) {end=i+4;break;}
  if(!end) {
   q->header_scan=q->used>3?q->used-3:0;
   if(q->used>65536) {*error="HTTP header limit";return -1;}return 0;
  }
  if(end>65536) {*error="HTTP header limit";return -1;}
  char *headers=malloc(end+1);if(!headers) {*error="out of memory";return -1;}
  memcpy(headers,q->data,end);headers[end]=0;
  char *save=NULL,*line=strtok_r(headers,"\r\n",&save);
  int minor=0,status=0;
  if(!line || sscanf(line,"HTTP/1.%d %d",&minor,&status)!=2 || (minor<0 || minor>1) || status<100 || status>599) {free(headers);*error="Invalid HTTP status";return -1;}
  int has_length=0,chunked=0,close_connection=minor==0,explicit_close=0;size_t length=0;
  while((line=strtok_r(NULL,"\r\n",&save))) {
   char *colon=strchr(line,':');if(!colon) {free(headers);*error="Invalid HTTP header";return -1;}
   *colon++=0;while(*colon==' ' || *colon=='\t') colon++;
   char *tail=colon+strlen(colon);while(tail>colon && (tail[-1]==' ' || tail[-1]=='\t')) *--tail=0;
   if(!strcasecmp(line,"content-length")) {
    if(has_length || !*colon) {free(headers);*error="Ambiguous HTTP length";return -1;}has_length=1;
    for(char *v=colon;*v;v++) {
     if(*v<'0' || *v>'9') {free(headers);*error="Invalid HTTP length";return -1;}
     if(length>(q->limit-(size_t)(*v-'0'))/10) {free(headers);*error="HTTP response exceeds byte limit";return -1;}
     length=length*10+(size_t)(*v-'0');
    }
   } else if(!strcasecmp(line,"transfer-encoding")) {
    if(chunked || strcasecmp(colon,"chunked")) {free(headers);*error="Unsupported HTTP transfer coding";return -1;}chunked=1;
   } else if(!strcasecmp(line,"connection")) {
    char *cs=NULL;for(char *v=strtok_r(colon,", \t",&cs);v;v=strtok_r(NULL,", \t",&cs)) {
     if(!strcasecmp(v,"close")) explicit_close=1;
     else if(minor==0 && !strcasecmp(v,"keep-alive")) close_connection=0;
    }
   }
  }
  free(headers);
  if(chunked && has_length) {*error="Ambiguous HTTP framing";return -1;}
  if(status<200) {
   if(status==101 || ++q->interim>8) {*error="Unsupported HTTP upgrade/interim response";return -1;}
   memmove(q->data,q->data+end,q->used-end);q->used-=end;q->header_scan=0;return frame(q,error);
  }
  q->headers_done=1;q->body_start=end;q->reusable=!close_connection && !explicit_close;
  if(status==204 || status==304) {q->framing=1;q->expected=end;}
  else if(chunked) {q->framing=2;q->chunk_pos=end;}
  else if(has_length) {
   if(length>q->limit-end) {*error="HTTP response exceeds byte limit";return -1;}
   q->framing=1;q->expected=end+length;
  } else {q->framing=3;q->reusable=0;}
 }
 if(q->framing==1) {
  if(q->used>q->expected) {*error="Data after framed HTTP response";return -1;}
  return q->used==q->expected;
 }
 if(q->framing==2) {
  for(;;) {
   if(q->chunk_state==1) {
    if(q->used-q->chunk_pos<q->chunk_size+2) return 0;
    q->chunk_pos+=q->chunk_size;
    if(memcmp(q->data+q->chunk_pos,"\r\n",2)) {*error="Invalid HTTP chunk terminator";return -1;}
    q->chunk_pos+=2;q->chunk_state=0;
   }
   size_t end=q->chunk_pos;
   while(end+1<q->used && memcmp(q->data+end,"\r\n",2)) end++;
   if(end+1>=q->used) {if(q->used-q->chunk_pos>8192) {*error="HTTP chunk line limit";return -1;}return 0;}
   if(q->chunk_state==2) {
    int last=end==q->chunk_pos;q->chunk_pos=end+2;
    if(last) {if(q->used!=q->chunk_pos) {*error="Data after HTTP trailers";return -1;}return 1;}
    continue;
   }
   size_t value=0;int digits=0;
   for(size_t i=q->chunk_pos;i<end && q->data[i]!=';';i++) {
    unsigned char c=q->data[i];int d=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;
    if(d<0) {*error="Invalid HTTP chunk size";return -1;}
    if(value>(q->limit-(size_t)d)/16) {*error="HTTP response exceeds byte limit";return -1;}
    value=value*16+(size_t)d;digits++;
   }
   if(!digits) {*error="Empty HTTP chunk size";return -1;}
   q->chunk_pos=end+2;
   if(value>q->limit-q->chunk_pos) {*error="HTTP response exceeds byte limit";return -1;}
   q->chunk_size=value;q->chunk_state=value?1:2;
  }
 }
 return 0;
}
static void finish(engine *r,request *q,const char *error) {
 if(q->phase==DONE) return;
 if(error && !r->stop && q->reused && !q->retried && !q->used && !q->cancelled && now_ms()<q->deadline) {
  if(q->ssl) SSL_free(q->ssl);
  if(q->fd>=0) close(q->fd);
  q->ssl=NULL;q->fd=-1;q->retried=1;q->sent=0;q->phase=QUEUED;return;
 }
 if(error) message(q->error,sizeof(q->error),error);
 if(!error && r->keepalive && q->reusable && !q->cancelled && q->framing!=3 && q->fd>=0) {
  int slot=0;for(int i=0;i<ACTIVE;i++) if(r->idle[i].fd<0 || r->idle[i].expires<r->idle[slot].expires) slot=i;
  drop_idle(&r->idle[slot]);idle_connection *c=&r->idle[slot];
  c->fd=q->fd;c->ssl=q->ssl;c->tls=q->tls;c->port=q->address.sin_port;
  message(c->host,sizeof(c->host),q->host);c->expires=now_ms()+5000;
  q->fd=-1;q->ssl=NULL;
 }
 if(q->ssl) { SSL_free(q->ssl); q->ssl=NULL; }
 if(q->fd>=0) { close(q->fd); q->fd=-1; }
 q->phase=DONE; pthread_cond_broadcast(&r->changed);pthread_cond_broadcast(&manager_changed);
}
static int tls_result(engine *r,request *q,int rc) {
 int e=SSL_get_error(q->ssl,rc);
 if(e==SSL_ERROR_WANT_READ) {q->want=POLLIN;return 0;}
 if(e==SSL_ERROR_WANT_WRITE) {q->want=POLLOUT;return 0;}
 if(e==SSL_ERROR_ZERO_RETURN && q->phase==READING) {finish(r,q,q->headers_done && q->framing==3 ? NULL : "Truncated HTTP response");return 0;}
 finish(r,q,"TLS failed (handshake, verification, or truncated transport)");return 0;
}
static void connected(engine *r,request *q,SSL_CTX *ctx) {
 if(!q->tls) {q->phase=SENDING;q->want=POLLOUT;return;}
 if(!ctx || !(q->ssl=SSL_new(ctx))) {finish(r,q,"TLS context unavailable");return;}
 struct in_addr ip;
 int verified_name = inet_pton(AF_INET,q->host,&ip)==1
   ? X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(q->ssl),q->host)
   : SSL_set1_host(q->ssl,q->host);
 if(verified_name!=1 || SSL_set_tlsext_host_name(q->ssl,q->host)!=1 || SSL_set_fd(q->ssl,q->fd)!=1) {
  finish(r,q,"TLS setup failed");return;
 }
 SSL_set_connect_state(q->ssl);q->phase=HANDSHAKE;q->want=POLLOUT;
}
static void start(engine *r,request *q,SSL_CTX *ctx) {
 for(int i=0;i<ACTIVE;i++) {
  idle_connection *c=&r->idle[i];if(c->fd<0) continue;
  struct pollfd check={c->fd,POLLIN,0};
  if(c->expires<=now_ms() || poll(&check,1,0)!=0) {drop_idle(c);continue;}
  if(r->keepalive && !q->retried && c->tls==q->tls && c->port==q->address.sin_port && !strcmp(c->host,q->host)) {
   q->fd=c->fd;q->ssl=c->ssl;q->reused=1;q->phase=SENDING;q->want=POLLOUT;
   c->fd=-1;c->ssl=NULL;return;
  }
 }
 q->fd=socket(AF_INET,SOCK_STREAM,0);
 if(q->fd<0) {finish(r,q,"socket failed");return;}
 if(fcntl(q->fd,F_SETFL,O_NONBLOCK)<0) {finish(r,q,"nonblocking mode failed");return;}
#ifdef SO_NOSIGPIPE
 int one=1;setsockopt(q->fd,SOL_SOCKET,SO_NOSIGPIPE,&one,sizeof(one));
#endif
 int rc=connect(q->fd,(struct sockaddr *)&q->address,sizeof(q->address));
 if(rc==0) connected(r,q,ctx);
 else if(errno==EINPROGRESS) {q->phase=CONNECTING;q->want=POLLOUT;}
 else finish(r,q,"connect failed");
}
static void advance(engine *r,request *q,SSL_CTX *ctx) {
 if(q->phase==CONNECTING) {
  int e=0;socklen_t n=sizeof(e);
  if(getsockopt(q->fd,SOL_SOCKET,SO_ERROR,&e,&n)<0 || e) {finish(r,q,"connect failed");return;}
  connected(r,q,ctx);
 }
 if(q->phase==HANDSHAKE) {
  ERR_clear_error();int n=SSL_connect(q->ssl);
  if(n!=1) {tls_result(r,q,n);return;}
  q->phase=SENDING;q->want=POLLOUT;
 }
 if(q->phase==SENDING) {
  int n;
  if(q->tls) {ERR_clear_error();n=SSL_write(q->ssl,q->request+q->sent,(int)(q->request_size-q->sent));}
  else n=(int)send(q->fd,q->request+q->sent,q->request_size-q->sent,0);
  if(n<=0) {
   if(q->tls) tls_result(r,q,n);
   else if(errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR) finish(r,q,"send failed");
   return;
  }
  q->sent+=(size_t)n;
  if(q->sent!=q->request_size) return;
  q->phase=READING;q->want=POLLIN;
 }
 if(q->phase==READING) {
  /* At most 16 KiB per ready connection per turn, including SSL_pending data. */
  if(q->capacity-q->used<16384 && q->capacity<q->limit) {
   size_t size=q->capacity ? q->capacity*2 : 16384;
   if(size>q->limit) size=q->limit;
   void *p=realloc(q->data,size);
   if(!p) {finish(r,q,"out of memory");return;}
   q->data=p;q->capacity=size;
  }
  unsigned char extra;
  void *dest=q->used==q->limit ? &extra : q->data+q->used;
  size_t room=q->used==q->limit ? 1 : q->capacity-q->used;
  if(room>16384) room=16384;
  int n;
  if(q->tls) {ERR_clear_error();n=SSL_read(q->ssl,dest,(int)room);}
  else n=(int)recv(q->fd,dest,room,0);
  if(n>0) {
   if(q->used==q->limit) {finish(r,q,"HTTP response exceeds byte limit");return;}
   q->used+=(size_t)n;q->want=POLLIN;
   const char *error=NULL;int complete=frame(q,&error);
   if(complete<0) finish(r,q,error);
   else if(complete>0) finish(r,q,NULL);
  } else if(q->tls) tls_result(r,q,n);
  else if(n==0) finish(r,q,q->headers_done && q->framing==3 ? NULL : "Truncated HTTP response");
  else if(errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR) finish(r,q,"receive failed");
 }
}
static void *worker(void *arg) {
 engine *r=arg;
 /* Block SIGPIPE only on the owner thread; never change application handlers. */
 sigset_t signals;sigemptyset(&signals);sigaddset(&signals,SIGPIPE);pthread_sigmask(SIG_BLOCK,&signals,NULL);
 SSL_CTX *ctx=SSL_CTX_new(TLS_client_method());
 if(ctx) {
  SSL_CTX_set_verify(ctx,SSL_VERIFY_PEER,NULL);
  if(SSL_CTX_set_default_verify_paths(ctx)!=1) {SSL_CTX_free(ctx);ctx=NULL;}
 }
 pthread_mutex_lock(&r->mutex);
 while(!r->stop) {
  int active=0;double now=now_ms();
  for(int i=0;i<ACTIVE;i++) if(r->idle[i].fd>=0 && r->idle[i].expires<=now) drop_idle(&r->idle[i]);
  for(int i=0;i<SLOTS;i++) {
   request *q=&r->slots[i];
   if(q->phase>FREE && q->phase<DONE && (q->cancelled || now>=q->deadline)) finish(r,q,q->cancelled ? "cancelled" : "deadline exceeded");
   if(q->phase>=CONNECTING && q->phase<DONE) active++;
  }
  for(int i=0;i<SLOTS && active<ACTIVE;i++) if(r->slots[i].phase==QUEUED) {start(r,&r->slots[i],ctx);if(r->slots[i].phase!=DONE) active++;}
  struct pollfd fds[SLOTS+1];int map[SLOTS+1];int count=1,timeout=-1;
  fds[0]=(struct pollfd){r->wake[0],POLLIN,0};
  for(int i=0;i<ACTIVE;i++) if(r->idle[i].fd>=0) {int ms=(int)(r->idle[i].expires-now_ms()+1);if(ms<0) ms=0;if(timeout<0 || ms<timeout) timeout=ms;}
  for(int i=0;i<SLOTS;i++) {
   request *q=&r->slots[i];
   if(q->phase>FREE && q->phase<DONE) {
    double left=q->deadline-now_ms();int ms=left<=0 ? 0 : (int)(left+1);
    if(timeout<0 || ms<timeout) timeout=ms;
   }
   if(q->phase>=CONNECTING && q->phase<DONE) {
    fds[count]=(struct pollfd){q->fd,q->want,0};map[count++]=i;
    if(q->ssl && q->phase==READING && SSL_pending(q->ssl)>0) timeout=0;
   }
  }
  pthread_mutex_unlock(&r->mutex);
  if(!manager_tick() && (timeout<0 || timeout>10))timeout=10; // retry contention without blocking on manager
  int rc=poll(fds,(nfds_t)count,timeout);
  pthread_mutex_lock(&r->mutex);
  if(rc<0 && errno!=EINTR) {r->stop=1;break;}
  if(fds[0].revents) {char bytes[64];while(read(r->wake[0],bytes,sizeof(bytes))>0) {}}
  for(int j=1;j<count;j++) {
   request *q=&r->slots[map[j]];
   if(q->cancelled || now_ms()>=q->deadline) continue;
   if(fds[j].revents || (q->ssl && q->phase==READING && SSL_pending(q->ssl)>0)) advance(r,q,ctx);
  }
 }
 for(int i=0;i<SLOTS;i++) if(r->slots[i].phase>FREE && r->slots[i].phase<DONE) finish(r,&r->slots[i],"reactor stopped");
 pthread_mutex_unlock(&r->mutex);
 for(int i=0;i<ACTIVE;i++) drop_idle(&r->idle[i]);
 if(ctx) SSL_CTX_free(ctx);
 return NULL;
}
static engine *engine_open(void) {
 engine *r=calloc(1,sizeof(*r));if(!r) return NULL;
 for(int i=0;i<ACTIVE;i++) r->idle[i].fd=-1;
 r->response_limit=2u*1024u*1024u;
 r->keepalive=!(getenv("TUCANIUM_HTTP_KEEPALIVE") && !strcmp(getenv("TUCANIUM_HTTP_KEEPALIVE"),"0"));
 if(pipe(r->wake)<0) {free(r);return NULL;}
 if(fcntl(r->wake[0],F_SETFL,O_NONBLOCK)<0 || fcntl(r->wake[1],F_SETFL,O_NONBLOCK)<0) {close(r->wake[0]);close(r->wake[1]);free(r);return NULL;}
 pthread_mutex_init(&r->mutex,NULL);pthread_cond_init(&r->changed,NULL);r->next_id=1;
 if(pthread_create(&r->thread,NULL,worker,r)!=0) {close(r->wake[0]);close(r->wake[1]);pthread_cond_destroy(&r->changed);pthread_mutex_destroy(&r->mutex);free(r);return NULL;}
 return r;
}
static int engine_submit(engine *r,const char *host,int port,const char *path,int tls,int timeout,char *error,size_t n,const struct sockaddr_in *resolved,size_t response_limit) {
 if(!r || !host || !path || strlen(host)>255 || !*host || path[0]!='/' || strpbrk(host,"\r\n \t") || strpbrk(path,"\r\n \t") || port<1 || port>65535 || timeout<1 || timeout>60000) {message(error,n,"invalid request");return -1;}
 struct sockaddr_in address=*resolved;
 pthread_mutex_lock(&r->mutex);
 request *q=NULL;
 if(!r->stop && r->next_id<INT_MAX) for(int i=0;i<SLOTS;i++) if(r->slots[i].phase==FREE) {q=&r->slots[i];break;}
 if(!q) {pthread_mutex_unlock(&r->mutex);message(error,n,"reactor full or stopped");return -1;}
 memset(q,0,sizeof(*q));q->fd=-1;q->limit=response_limit;q->address=address;q->tls=tls;message(q->host,sizeof(q->host),host);
 int len=snprintf(q->request,sizeof(q->request),"GET %s HTTP/1.1\r\nHost: %s:%d\r\nConnection: %s\r\nAccept-Encoding: identity\r\nUser-Agent: AmLang-Reactor-Prototype\r\n\r\n",path,host,port,r->keepalive?"keep-alive":"close");
 if(len<0 || (size_t)len>=sizeof(q->request)) {pthread_mutex_unlock(&r->mutex);message(error,n,"request too large");return -1;}
 q->request_size=(size_t)len;q->deadline=now_ms()+timeout;q->id=r->next_id++;q->phase=QUEUED;
 int id=q->id;wakeup(r);pthread_mutex_unlock(&r->mutex);return id;
}
static int engine_take(engine *r,int id,int wait,unsigned char **data,size_t *size,char *error,size_t n) {
 *data=NULL;*size=0;
 if(!r) {message(error,n,"reactor closed");return -1;}
 pthread_mutex_lock(&r->mutex);request *q=find(r,id);
 if(!q) {pthread_mutex_unlock(&r->mutex);message(error,n,"unknown or consumed request");return -1;}
 while(wait && q->phase!=DONE) pthread_cond_wait(&r->changed,&r->mutex);
 if(q->phase!=DONE) {pthread_mutex_unlock(&r->mutex);return 0;}
 int rc=1;
 if(q->error[0]) {message(error,n,q->error);free(q->data);rc=-1;}
 else {*data=q->data;*size=q->used;}
 memset(q,0,sizeof(*q));wakeup(r);pthread_mutex_unlock(&r->mutex);return rc;
}
static void engine_cancel(engine *r,int id) {
 if(!r) return;
 pthread_mutex_lock(&r->mutex);request *q=find(r,id);if(q) q->cancelled=1;wakeup(r);pthread_mutex_unlock(&r->mutex);
}
static void engine_close(engine *r) {
 if(!r) return;
 pthread_mutex_lock(&r->mutex);r->stop=1;wakeup(r);pthread_mutex_unlock(&r->mutex);pthread_join(r->thread,NULL);
 for(int i=0;i<SLOTS;i++) free(r->slots[i].data);
 close(r->wake[0]);close(r->wake[1]);pthread_cond_destroy(&r->changed);pthread_mutex_destroy(&r->mutex);free(r);
}

/* Process-wide manager. Each engine is an independent poll loop with its own
 * mutex, TLS context and keep-alive cache. Manager state never crosses into an
 * AmLang heap. Client handles/results remain local to their original caller. */
typedef struct managed_worker { engine *loop; int serial; struct managed_worker *next; } managed_worker;
typedef struct {
 int id, backend_id, state, worker_serial;
 char host[256], path[4096]; int port, tls;
 struct sockaddr_in address; double deadline; size_t limit;
 managed_worker *owner;
 unsigned char *data; size_t size;
 char error[192];
} managed_request;
struct am_reactor {
 managed_request requests[SLOTS];
 int next_id, closing;
 size_t response_limit;
 struct am_reactor *next;
};
static pthread_mutex_t manager_mutex=PTHREAD_MUTEX_INITIALIZER;
static managed_worker *manager_workers;
static am_reactor *manager_clients;
static int manager_limit=4, manager_count, manager_serial, manager_reconfiguring;
static void manager_pause(int milliseconds) {
 struct timespec t;clock_gettime(CLOCK_REALTIME,&t);
 t.tv_sec+=milliseconds/1000;t.tv_nsec+=(milliseconds%1000)*1000000L;
 if(t.tv_nsec>=1000000000L){t.tv_sec++;t.tv_nsec-=1000000000L;}
 /* Completion signals are sent without taking this mutex (engine -> manager
  * would invert the lock order). A bounded wait also covers a missed signal. */
 pthread_cond_timedwait(&manager_changed,&manager_mutex,&t);
}
static managed_request *manager_find(am_reactor *r,int id) {
 for(int i=0;i<SLOTS;i++)if(r->requests[i].state && r->requests[i].id==id)return &r->requests[i];
 return NULL;
}
static void manager_collect(managed_request *q) {
 if(q->state!=1)return;
 int result=engine_take(q->owner->loop,q->backend_id,0,&q->data,&q->size,q->error,sizeof(q->error));
 if(result){q->state=2;q->owner=NULL;}
}
static void manager_collect_all(void) {
 for(am_reactor *r=manager_clients;r;r=r->next)for(int i=0;i<SLOTS;i++)manager_collect(&r->requests[i]);
}
static int engine_load(engine *e,const char *host,int port,int tls,int *cached) {
 int load=0;*cached=0;pthread_mutex_lock(&e->mutex);
 for(int i=0;i<SLOTS;i++)if(e->slots[i].phase!=FREE)load++;
 for(int i=0;i<ACTIVE;i++)if(e->idle[i].fd>=0 && e->idle[i].expires>now_ms() && e->idle[i].tls==tls && e->idle[i].port==htons((unsigned short)port) && !strcmp(e->idle[i].host,host))*cached=1;
 if(e->stop && *host)load=SLOTS;
 pthread_mutex_unlock(&e->mutex);return load;
}
static managed_worker *manager_choose(const char *host,int port,int tls) {
 managed_worker *best=NULL,*warm=NULL;int best_load=SLOTS,warm_load=SLOTS;
 manager_collect_all();
 for(managed_worker *w=manager_workers;w;w=w->next){int cached=0,load=engine_load(w->loop,host,port,tls,&cached);
  if(load<best_load){best=w;best_load=load;}
  if(cached && load<ACTIVE && load<warm_load){warm=w;warm_load=load;}
 }
 if(warm)return warm;
 if(manager_count<manager_limit && best_load>0){
  managed_worker *w=calloc(1,sizeof(*w));
  if(w){w->loop=engine_open();if(w->loop){w->serial=++manager_serial;w->next=manager_workers;manager_workers=w;manager_count++;return w;}free(w);}
 }
 return best;
}
static void manager_progress(void) {
 manager_collect_all();
 if(manager_reconfiguring)return;
 for(am_reactor *r=manager_clients;r;r=r->next)for(int i=0;i<SLOTS;i++){
  managed_request *q=&r->requests[i];if(q->state!=3)continue;
  double left=q->deadline-now_ms();
  if(left<=0){message(q->error,sizeof(q->error),"deadline exceeded");q->state=2;continue;}
  managed_worker *w=manager_choose(q->host,q->port,q->tls);if(!w)continue;
  int timeout=(int)left;if(timeout<1)timeout=1;if(timeout>60000)timeout=60000;
  int id=engine_submit(w->loop,q->host,q->port,q->path,q->tls,timeout,q->error,sizeof(q->error),&q->address,q->limit);
  if(id<0){q->state=2;continue;}
  q->backend_id=id;q->owner=w;q->worker_serial=w->serial;q->state=1;
 }
}
static int manager_tick(void) {
 if(pthread_mutex_trylock(&manager_mutex)!=0)return 0;
 manager_progress();pthread_mutex_unlock(&manager_mutex);return 1;
}
int am_reactor_get_max_threads(void){pthread_mutex_lock(&manager_mutex);int n=manager_limit;pthread_mutex_unlock(&manager_mutex);return n;}
int am_reactor_worker_count(void){pthread_mutex_lock(&manager_mutex);int n=manager_count;pthread_mutex_unlock(&manager_mutex);return n;}
int am_reactor_set_max_threads(int maximum){
 if(maximum<1)return -1;
 pthread_mutex_lock(&manager_mutex);
 while(manager_reconfiguring)manager_pause(10);
 manager_reconfiguring=1;
 while(manager_count>maximum){
  managed_worker *retire=manager_workers;
  /* Stop admission while draining. Completed bytes are moved to client
   * result slots, so the setter never waits for a caller to consume results. */
  for(;;){manager_collect_all();int cached=0;if(engine_load(retire->loop,"",0,0,&cached)==0)break;manager_pause(10);}
  manager_workers=retire->next;engine_close(retire->loop);free(retire);manager_count--;
 }
 manager_limit=maximum;manager_reconfiguring=0;manager_progress();pthread_cond_broadcast(&manager_changed);pthread_mutex_unlock(&manager_mutex);return 0;
}
am_reactor *am_reactor_open(void){
 am_reactor *r=calloc(1,sizeof(*r));if(!r)return NULL;r->response_limit=2u*1024u*1024u;r->next_id=1;
 pthread_mutex_lock(&manager_mutex);r->next=manager_clients;manager_clients=r;pthread_mutex_unlock(&manager_mutex);return r;
}
int am_reactor_set_limit(am_reactor *r,size_t limit){
 if(!r || !limit || limit>CAP)return -1;
 pthread_mutex_lock(&manager_mutex);r->response_limit=limit;pthread_mutex_unlock(&manager_mutex);return 0;
}
int am_reactor_submit(am_reactor *r,const char *host,int port,const char *path,int tls,int timeout,char *error,size_t n){
 if(!r || !host || !path || strlen(host)>255 || !*host || path[0]!='/' || strpbrk(host,"\r\n \t") || strpbrk(path,"\r\n \t") || port<1 || port>65535 || timeout<1 || timeout>60000){message(error,n,"invalid request");return -1;}
 /* DNS remains on the submitting caller, outside both manager and socket
  * loops. It cannot block other clients or a worker's nonblocking sockets. */
 struct addrinfo hints={0},*addresses=NULL;hints.ai_family=AF_INET;hints.ai_socktype=SOCK_STREAM;
 if(getaddrinfo(host,NULL,&hints,&addresses)!=0){message(error,n,"DNS failed");return -1;}
 struct sockaddr_in address=*(struct sockaddr_in *)addresses->ai_addr;freeaddrinfo(addresses);address.sin_port=htons((unsigned short)port);
 pthread_mutex_lock(&manager_mutex);
 while(manager_reconfiguring)manager_pause(10);
 managed_request *q=NULL;
 if(!r->closing && r->next_id<INT_MAX)for(int i=0;i<SLOTS;i++)if(!r->requests[i].state){q=&r->requests[i];break;}
 if(!q){message(error,n,"reactor full or stopped");pthread_mutex_unlock(&manager_mutex);return -1;}
 if(strlen(path)>=sizeof(q->path)){message(error,n,"request too large");pthread_mutex_unlock(&manager_mutex);return -1;}
 q->id=r->next_id++;q->port=port;q->tls=tls;q->address=address;q->deadline=now_ms()+timeout;q->limit=r->response_limit;
 message(q->host,sizeof(q->host),host);message(q->path,sizeof(q->path),path);q->state=3;
 manager_progress();int id=q->id;pthread_mutex_unlock(&manager_mutex);return id;
}
int am_reactor_ready(am_reactor *r,int id){
 if(!r)return -1;pthread_mutex_lock(&manager_mutex);manager_progress();managed_request *q=manager_find(r,id);int result=q?q->state==2:-1;pthread_mutex_unlock(&manager_mutex);return result;
}
int am_reactor_request_worker(am_reactor *r,int id){
 if(!r)return -1;pthread_mutex_lock(&manager_mutex);managed_request *q=manager_find(r,id);int result=q?q->worker_serial:-1;pthread_mutex_unlock(&manager_mutex);return result;
}
int am_reactor_wait_any(am_reactor *r,int timeout){
 if(!r || timeout<0 || timeout>1000)return -1;
 double deadline=now_ms()+timeout;pthread_mutex_lock(&manager_mutex);
 for(;;){manager_progress();for(int i=0;i<SLOTS;i++){if(r->requests[i].state==2){int id=r->requests[i].id;pthread_mutex_unlock(&manager_mutex);return id;}}
  if(r->closing){pthread_mutex_unlock(&manager_mutex);return -1;}
  double left=deadline-now_ms();if(left<=0){pthread_mutex_unlock(&manager_mutex);return 0;}manager_pause(left>10?10:(int)left+1);
 }
}
int am_reactor_take(am_reactor *r,int id,int wait,unsigned char **data,size_t *size,char *error,size_t n){
 *data=NULL;*size=0;if(!r){message(error,n,"reactor closed");return -1;}
 pthread_mutex_lock(&manager_mutex);managed_request *q=manager_find(r,id);
 if(!q){message(error,n,"unknown or consumed request");pthread_mutex_unlock(&manager_mutex);return -1;}
 for(;;){manager_progress();if(q->state==2)break;if(!wait){pthread_mutex_unlock(&manager_mutex);return 0;}manager_pause(10);}
 int result=1;if(q->error[0]){message(error,n,q->error);free(q->data);result=-1;}else{*data=q->data;*size=q->size;}
 memset(q,0,sizeof(*q));pthread_mutex_unlock(&manager_mutex);return result;
}
void am_reactor_cancel(am_reactor *r,int id){
 if(!r)return;pthread_mutex_lock(&manager_mutex);managed_request *q=manager_find(r,id);if(q && q->state==1)engine_cancel(q->owner->loop,q->backend_id);else if(q && q->state==3){message(q->error,sizeof(q->error),"cancelled");q->state=2;}pthread_mutex_unlock(&manager_mutex);
}
void am_reactor_close(am_reactor *r){
 if(!r)return;pthread_mutex_lock(&manager_mutex);
 /* Setter owns the worker list while its condition wait temporarily drops
  * this lock. Do not remove its engines underneath it. */
 while(manager_reconfiguring)manager_pause(10);
 manager_reconfiguring=1;r->closing=1;
 for(int i=0;i<SLOTS;i++)if(r->requests[i].state==1)engine_cancel(r->requests[i].owner->loop,r->requests[i].backend_id);
 for(int i=0;i<SLOTS;i++){managed_request *q=&r->requests[i];while(q->state==1){manager_collect(q);if(q->state==1)manager_pause(10);}free(q->data);}
 am_reactor **p=&manager_clients;while(*p && *p!=r)p=&(*p)->next;if(*p)*p=r->next;
 if(!manager_clients){while(manager_workers){managed_worker *w=manager_workers;manager_workers=w->next;engine_close(w->loop);free(w);}manager_count=0;}
 manager_reconfiguring=0;manager_progress();pthread_cond_broadcast(&manager_changed);pthread_mutex_unlock(&manager_mutex);free(r);
}

#ifndef AM_REACTOR_STANDALONE
#include <libc/core.h>
#include <libc/core_inline_functions.h>
#include <Am/Web/Client/Experimental/HttpReactor.h>
#include <Am/Lang/String.h>
#include <Am/Lang/Array.h>
#include <Am/Lang/UByte.h>
function_result Am_Web_Client_Experimental_HttpReactor_getMaxThreads_0(void) { function_result r={.has_return_value=true};r.return_value.value.int_value=am_reactor_get_max_threads();return r; }
function_result Am_Web_Client_Experimental_HttpReactor_setMaxThreads_0(int maximum) { function_result r={0};if(am_reactor_set_max_threads(maximum)<0)__throw_simple_exception("maxThreads must be at least 1","HttpReactor",&r);return r; }
function_result Am_Web_Client_Experimental_HttpReactor_workerCount_0(void) { function_result r={.has_return_value=true};r.return_value.value.int_value=am_reactor_worker_count();return r; }
function_result Am_Web_Client_Experimental_HttpReactor__native_mark_children_0(aobject * const self) {
 (void)self; function_result r={0}; return r;
}
static am_reactor *service(aobject *self) {
 return self->object_properties.class_object_properties.object_data.value.custom_value;
}
function_result Am_Web_Client_Experimental_HttpReactor__native_init_0(aobject * const self) {
 function_result r={0};
 self->object_properties.class_object_properties.object_data.value.custom_value=am_reactor_open();
 if(!service(self)) __throw_simple_exception("Cannot start HTTP reactor","HttpReactor",&r);
 return r;
}
function_result Am_Web_Client_Experimental_HttpReactor_closeNative_0(aobject * const self) {
 function_result r={0};am_reactor_close(service(self));
 self->object_properties.class_object_properties.object_data.value.custom_value=NULL;return r;
}
function_result Am_Web_Client_Experimental_HttpReactor__native_release_0(aobject * const self) {
 return Am_Web_Client_Experimental_HttpReactor_closeNative_0(self);
}
function_result Am_Web_Client_Experimental_HttpReactor_setLimitNative_0(aobject * const self,int limit) {
 function_result r={0};if(am_reactor_set_limit(service(self),(size_t)limit)<0) __throw_simple_exception("Invalid reactor response limit","HttpReactor",&r);return r;
}
function_result Am_Web_Client_Experimental_HttpReactor_submitNative_0(aobject * const self,aobject *host,int port,aobject *path,bool tls,int timeout) {
 function_result r={.has_return_value=true};char error[192]={0};
 string_holder *h=host->object_properties.class_object_properties.object_data.value.custom_value;
 string_holder *p=path->object_properties.class_object_properties.object_data.value.custom_value;
 int id=am_reactor_submit(service(self),h->string_value,port,p->string_value,tls,timeout,error,sizeof(error));
 if(id<0) __throw_simple_exception_copy(error,"HttpReactor.submit",&r);
 else r.return_value.value.int_value=id;
 return r;
}
function_result Am_Web_Client_Experimental_HttpReactor_ready_0(aobject * const self,int id) {
 function_result r={.has_return_value=true};r.return_value.value.int_value=am_reactor_ready(service(self),id);return r;
}
function_result Am_Web_Client_Experimental_HttpReactor_waitAny_0(aobject * const self,int timeout) {
 function_result r={.has_return_value=true};r.return_value.value.int_value=am_reactor_wait_any(service(self),timeout);return r;
}
function_result Am_Web_Client_Experimental_HttpReactor_cancel_0(aobject * const self,int id) {
 function_result r={0};am_reactor_cancel(service(self),id);return r;
}
function_result Am_Web_Client_Experimental_HttpReactor_takeRaw_0(aobject * const self,int id) {
 function_result r={.has_return_value=true};unsigned char *data=NULL;size_t size=0;char error[192]={0};
 if(am_reactor_take(service(self),id,1,&data,&size,error,sizeof(error))<0) {
  __throw_simple_exception_copy(error,"HttpReactor.take",&r);return r;
 }
 aobject *array=__create_array((unsigned int)size,1,&Am_Lang_Array_ta_Am_Lang_UByte,uchar_type);
 array_holder *holder=(array_holder *)&array[1];
 if(size) memcpy(holder->array_data,data,size);
 free(data);r.return_value.value.object_value=array;return r;
}
#endif
