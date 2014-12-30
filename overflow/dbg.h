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

#ifdef __cplusplus
}
#endif

#endif /* __DBG_H_INCLUDED__ */