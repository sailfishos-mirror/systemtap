################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../runtime/alloc.c \
../runtime/arith.c \
../runtime/autoconf-constant-tsc.c \
../runtime/autoconf-dpath-path.c \
../runtime/autoconf-hrtimer-rel.c \
../runtime/autoconf-inode-private.c \
../runtime/autoconf-ktime-get-real.c \
../runtime/autoconf-nameidata.c \
../runtime/autoconf-oneachcpu-retry.c \
../runtime/autoconf-probe-kernel.c \
../runtime/autoconf-real-parent.c \
../runtime/autoconf-tsc-khz.c \
../runtime/autoconf-uaccess.c \
../runtime/autoconf-unregister-kprobes.c \
../runtime/autoconf-x86-uniregs.c \
../runtime/copy.c \
../runtime/counter.c \
../runtime/io.c \
../runtime/itrace.c \
../runtime/map-gen.c \
../runtime/map-stat.c \
../runtime/map.c \
../runtime/mempool.c \
../runtime/perf.c \
../runtime/pmap-gen.c \
../runtime/print.c \
../runtime/print_new.c \
../runtime/print_old.c \
../runtime/procfs.c \
../runtime/regs-ia64.c \
../runtime/regs.c \
../runtime/stack-arm.c \
../runtime/stack-i386.c \
../runtime/stack-ia64.c \
../runtime/stack-ppc64.c \
../runtime/stack-s390.c \
../runtime/stack-x86_64.c \
../runtime/stack.c \
../runtime/stat-common.c \
../runtime/stat.c \
../runtime/string.c \
../runtime/sym.c \
../runtime/task_finder.c \
../runtime/task_finder_vma.c \
../runtime/time.c \
../runtime/unwind.c \
../runtime/vsprintf.c 

OBJS += \
./runtime/alloc.o \
./runtime/arith.o \
./runtime/autoconf-constant-tsc.o \
./runtime/autoconf-dpath-path.o \
./runtime/autoconf-hrtimer-rel.o \
./runtime/autoconf-inode-private.o \
./runtime/autoconf-ktime-get-real.o \
./runtime/autoconf-nameidata.o \
./runtime/autoconf-oneachcpu-retry.o \
./runtime/autoconf-probe-kernel.o \
./runtime/autoconf-real-parent.o \
./runtime/autoconf-tsc-khz.o \
./runtime/autoconf-uaccess.o \
./runtime/autoconf-unregister-kprobes.o \
./runtime/autoconf-x86-uniregs.o \
./runtime/copy.o \
./runtime/counter.o \
./runtime/io.o \
./runtime/itrace.o \
./runtime/map-gen.o \
./runtime/map-stat.o \
./runtime/map.o \
./runtime/mempool.o \
./runtime/perf.o \
./runtime/pmap-gen.o \
./runtime/print.o \
./runtime/print_new.o \
./runtime/print_old.o \
./runtime/procfs.o \
./runtime/regs-ia64.o \
./runtime/regs.o \
./runtime/stack-arm.o \
./runtime/stack-i386.o \
./runtime/stack-ia64.o \
./runtime/stack-ppc64.o \
./runtime/stack-s390.o \
./runtime/stack-x86_64.o \
./runtime/stack.o \
./runtime/stat-common.o \
./runtime/stat.o \
./runtime/string.o \
./runtime/sym.o \
./runtime/task_finder.o \
./runtime/task_finder_vma.o \
./runtime/time.o \
./runtime/unwind.o \
./runtime/vsprintf.o 

C_DEPS += \
./runtime/alloc.d \
./runtime/arith.d \
./runtime/autoconf-constant-tsc.d \
./runtime/autoconf-dpath-path.d \
./runtime/autoconf-hrtimer-rel.d \
./runtime/autoconf-inode-private.d \
./runtime/autoconf-ktime-get-real.d \
./runtime/autoconf-nameidata.d \
./runtime/autoconf-oneachcpu-retry.d \
./runtime/autoconf-probe-kernel.d \
./runtime/autoconf-real-parent.d \
./runtime/autoconf-tsc-khz.d \
./runtime/autoconf-uaccess.d \
./runtime/autoconf-unregister-kprobes.d \
./runtime/autoconf-x86-uniregs.d \
./runtime/copy.d \
./runtime/counter.d \
./runtime/io.d \
./runtime/itrace.d \
./runtime/map-gen.d \
./runtime/map-stat.d \
./runtime/map.d \
./runtime/mempool.d \
./runtime/perf.d \
./runtime/pmap-gen.d \
./runtime/print.d \
./runtime/print_new.d \
./runtime/print_old.d \
./runtime/procfs.d \
./runtime/regs-ia64.d \
./runtime/regs.d \
./runtime/stack-arm.d \
./runtime/stack-i386.d \
./runtime/stack-ia64.d \
./runtime/stack-ppc64.d \
./runtime/stack-s390.d \
./runtime/stack-x86_64.d \
./runtime/stack.d \
./runtime/stat-common.d \
./runtime/stat.d \
./runtime/string.d \
./runtime/sym.d \
./runtime/task_finder.d \
./runtime/task_finder_vma.d \
./runtime/time.d \
./runtime/unwind.d \
./runtime/vsprintf.d 


# Each subdirectory must supply rules for building sources it contributes
runtime/%.o: ../runtime/%.c
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C Compiler'
	gcc -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -o"$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


