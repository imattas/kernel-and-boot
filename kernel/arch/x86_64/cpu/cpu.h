#ifndef OS_X86_64_CPU_H
#define OS_X86_64_CPU_H
#include <stdint.h>
typedef struct { uint32_t maximum_basic_leaf, maximum_extended_leaf, features_ecx, features_edx; char vendor[13]; } arch_cpu_info_t;
const arch_cpu_info_t *arch_cpu_initialize(void);
#endif
