/* test_relay_sweep.c - the real relay (tl_relay, started here on 127.0.0.1 on a free port) against someone
 * sweeping for live game codes from one address: after its 10 wrong codes it hears nothing more, not even for a
 * live code, and a game someone is playing answers exactly as a wrong code does. Original code for this project.
 *
 * usage: test_relay_sweep PATH_TO_TL_RELAY */
#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

enum { VERSION = 10, T_RHOST = 6, T_RCODE = 7, T_RJOIN = 8, T_RFAIL = 9 };

static int fails;
#define CHECK(c)                                                       \
  do {                                                                 \
    if (!(c)) {                                                        \
      fprintf(stderr, "test_relay_sweep:%d: %s\n", __LINE__, #c);      \
      fails++;                                                         \
    }                                                                  \
  } while (0)

static struct sockaddr_in relay;
static pid_t child = -1;

static int sock(void) {
  int s = socket(AF_INET, SOCK_DGRAM, 0);
  if (s < 0) {
    perror("test_relay_sweep: socket");
    exit(1);
  }
  struct timeval tv = { 0, 200000 }; /* 0.2 s: the relay answers at once, so silence is silence */
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  return s;
}
static void send_code(int s, int version, int type, const char *code) {
  unsigned char p[11] = { 'A', '4', 'C', (unsigned char)version, (unsigned char)type };
  memcpy(p + 5, code, 6);
  sendto(s, p, sizeof p, 0, (const struct sockaddr *)&relay, sizeof relay);
}
/* the reply's length (0: none), its bytes in p */
static int answer(int s, unsigned char *p, size_t size) {
  ssize_t n = recv(s, p, size, 0);
  return n < 0 ? 0 : (int)n;
}
static int ask(int s, int type, const char *code) {
  unsigned char p[64];
  send_code(s, VERSION, type, code);
  int n = answer(s, p, sizeof p);
  return n >= 5 ? p[4] : 0;
}

static void stop(void) {
  if (child > 0) {
    kill(child, SIGTERM);
    waitpid(child, NULL, 0);
  }
  child = -1;
}

/* a new relay on a free port, answering */
static int start(const char *exe) {
  stop();
  int s = socket(AF_INET, SOCK_DGRAM, 0);
  struct sockaddr_in a;
  memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(0x7F000001u) /* 127.0.0.1 */;
  socklen_t al = sizeof a;
  if (s < 0 || bind(s, (struct sockaddr *)&a, sizeof a) != 0 || getsockname(s, (struct sockaddr *)&a, &al) != 0) return 0;
  close(s);
  char port[16];
  snprintf(port, sizeof port, "%d", ntohs(a.sin_port));
  child = fork();
  if (child == 0) {
    if (!freopen("/dev/null", "w", stdout)) _exit(127);
    execl(exe, exe, port, (char *)NULL);
    _exit(127);
  }
  relay = a;
  int probe = sock();
  for (int i = 0; i < 50; i++) { /* another version's packet: five bytes back, and no guess spent */
    unsigned char p[64];
    send_code(probe, VERSION + 1, T_RJOIN, "000000");
    if (answer(probe, p, sizeof p) == 5) {
      close(probe);
      return 1;
    }
  }
  close(probe);
  return 0;
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: test_relay_sweep PATH_TO_TL_RELAY\n");
    return 2;
  }
  signal(SIGPIPE, SIG_IGN);
  /* a sweep from one address: 10 wrong codes answered, then silence, for the live code as well */
  CHECK(start(argv[1]));
  int host = sock(), atk = sock(), guest = sock();
  CHECK(ask(host, T_RHOST, "004711") == T_RCODE);
  int fail = 0;
  for (int c = 0; c < 10; c++) {
    char code[8];
    snprintf(code, sizeof code, "%06d", c);
    fail += ask(atk, T_RJOIN, code) == T_RFAIL;
  }
  CHECK(fail == 10);
  CHECK(ask(atk, T_RJOIN, "004711") == 0); /* before the fix: T_RJOIN, the live code found */
  CHECK(ask(atk, T_RJOIN, "000010") == 0);
  close(host), close(atk), close(guest);

  /* the guest joins and asks again for free; a game being played answers as a wrong code does */
  CHECK(start(argv[1]));
  host = sock(), atk = sock(), guest = sock();
  CHECK(ask(host, T_RHOST, "004711") == T_RCODE);
  CHECK(ask(guest, T_RJOIN, "004711") == T_RJOIN);
  for (int i = 0; i < 20; i++) CHECK(ask(guest, T_RJOIN, "004711") == T_RJOIN);
  unsigned char a1[64], a2[64];
  send_code(atk, VERSION, T_RJOIN, "004711");
  int n1 = answer(atk, a1, sizeof a1);
  send_code(atk, VERSION, T_RJOIN, "123456");
  int n2 = answer(atk, a2, sizeof a2);
  CHECK(n1 == 5 && a1[4] == T_RFAIL);
  CHECK(n1 == n2 && !memcmp(a1, a2, (size_t)n1));
  close(host), close(atk), close(guest);

  /* another version's request gets five bytes back; another version's five-byte packet (such as that answer, from
   * a second relay) gets nothing, so two relays can't keep answering each other */
  CHECK(start(argv[1]));
  int o = sock();
  unsigned char r[64];
  send_code(o, VERSION + 1, T_RJOIN, "000001");
  CHECK(answer(o, r, sizeof r) == 5 && r[3] == VERSION);
  const unsigned char five[5] = { 'A', '4', 'C', VERSION + 1, T_RJOIN };
  sendto(o, five, sizeof five, 0, (const struct sockaddr *)&relay, sizeof relay);
  CHECK(answer(o, r, sizeof r) == 0);
  close(o);
  stop();
  if (fails) fprintf(stderr, "test_relay_sweep: %d failed\n", fails);
  else printf("test_relay_sweep: ok\n");
  return fails != 0;
}
