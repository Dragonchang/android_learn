#ifndef _PARTNER_HTC_SSDTEST_UTILS_H_
#define _PARTNER_HTC_SSDTEST_UTILS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <utils/Log.h>

#define E(...)					{LOGE(__VA_ARGS__); printf(__VA_ARGS__);}
#define D(...)					LOGD(__VA_ARGS__)
#define E_SUCCESS				(1)
#define E_FAILURE				(-1)

#define ARG_EXISTED(arg_existed)			((arg_existed++) > 0)
#define ARG_VAL_PRESENT(optarg)				(optarg && optarg[0] != '-')
#define ARG_VAL_LEGAL(argv, optind)			(!argv[optind] || argv[optind][0] == '-')


#ifdef __cplusplus
} // extern "C"
#endif

#endif /* _PARTNER_HTC_SSDTEST_UTILS_H_ */
