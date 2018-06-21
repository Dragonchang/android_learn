#ifndef _SSD_CPU_H_
#define _SSD_CPU_H_

#if __cplusplus
extern "C" {
#endif

#ifndef UNUSED_VAR
#define UNUSED_VAR(x) UNUSED_ ## x __attribute__((__unused__))
#endif

extern float cpu_usage (void *reserved);

#if __cplusplus
}
#endif

#endif
