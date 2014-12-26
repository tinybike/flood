#ifndef __DBG_H_INCLUDED__
#define __DBG_H_INCLUDED__

#ifdef __cplusplus
extern "C" {
#endif

#ifdef NDEBUG
#define debug(format, args...) ((void)0)
#else
#define debug printf
#endif

#define ERROR(msg, p) fprintf(stderr, "ERROR: " msg " %s\n", (p));

#define clean_errno() (errno == 0 ? "None" : strerror(errno))

#define log_err(M, ...) fprintf(stderr, "[ERROR] (%s:%d: errno: %s) " M "\n", __FILE__, __LINE__, clean_errno(), ##__VA_ARGS__)

#define log_warn(M, ...) fprintf(stderr, "[WARN] (%s:%d: errno: %s) " M "\n", __FILE__, __LINE__, clean_errno(), ##__VA_ARGS__)

#define log_info(M, ...) fprintf(stderr, "[INFO] (%s:%d) " M "\n", __FILE__, __LINE__, ##__VA_ARGS__)

#define check(A, ...) if(!(A)) { log_err("", ##__VA_ARGS__); errno=0; goto error; }

#define sentinel(M, ...)  { log_err(M, ##__VA_ARGS__); errno=0; goto error; }

#define check_mem(A) check((A), "Out of memory.")

#define check_debug(A, M, ...) if(!(A)) { debug(M, ##__VA_ARGS__); errno=0; goto error; }

#ifdef __cplusplus
}
#endif

#endif /* __DBG_H_INCLUDED__ */