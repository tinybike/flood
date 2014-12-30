#include <stdio.h>;
#include <signal.h>;
#include <stdlib.h>;
#include <stdarg.h>;

static void signal_handler(int);
static void dumpstack(void);
static void cleanup(void);
void init_signals(void);
void panic(const char *, ...);

struct sigaction sigact;
char *progname;

int main(int argc, char **argv)
{
    char *s;
    progname = *(argv);
    atexit(cleanup);
    init_signals();
    printf("About to seg fault by assigning zero to *s\n");
    *s = 0;
    sigemptyset(&sigact.sa_mask);
    return 0;
}

void init_signals(void)
{
    sigact.sa_handler = signal_handler;
    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = 0;
    sigaction(SIGINT, &sigact, (struct sigaction *)NULL);

    sigaddset(&sigact.sa_mask, SIGSEGV);
    sigaction(SIGSEGV, &sigact, (struct sigaction *)NULL);

    sigaddset(&sigact.sa_mask, SIGBUS);
    sigaction(SIGBUS, &sigact, (struct sigaction *)NULL);

    sigaddset(&sigact.sa_mask, SIGQUIT);
    sigaction(SIGQUIT, &sigact, (struct sigaction *)NULL);

    sigaddset(&sigact.sa_mask, SIGHUP);
    sigaction(SIGHUP, &sigact, (struct sigaction *)NULL);

    sigaddset(&sigact.sa_mask, SIGKILL);
    sigaction(SIGKILL, &sigact, (struct sigaction *)NULL);
}

static void signal_handler(int sig)
{
    if (sig == SIGHUP) panic("FATAL: Program hanged up\n");
    if (sig == SIGSEGV || sig == SIGBUS){
        dumpstack();
        panic("FATAL: %s Fault. Logged StackTrace\n", (sig == SIGSEGV) ? "Segmentation" : ((sig == SIGBUS) ? "Bus" : "Unknown"));
    }
    if (sig == SIGQUIT) panic("QUIT signal ended program\n");
    if (sig == SIGKILL) panic("KILL signal ended program\n");
    if (sig == SIGINT) ;
}

void panic(const char *fmt, ...)
{
    char buf[50];
    va_list argptr;
    va_start(argptr, fmt);
    vsprintf(buf, fmt, argptr);
    va_end(argptr);
    fprintf(stderr, buf);
    exit(-1);
}

static void dumpstack(void)
{
    /* Got this routine from http://www.whitefang.com/unix/faq_toc.html
    ** Section 6.5. Modified to redirect to file to prevent clutter
    */
    /* This needs to be changed... */
    char dbx[160];

    sprintf(dbx, "echo 'where\ndetach' | gdb -a %d > %s.dump", getpid(), progname);
    /* Change the dbx to gdb (need -q flag?) */

    system(dbx);
    return;
}

void cleanup(void)
{
    sigemptyset(&sigact.sa_mask);
    /* Do any cleaning up chores here */
}
