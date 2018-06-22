################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../testsuite/systemtap.syscall/access.c \
../testsuite/systemtap.syscall/acct.c \
../testsuite/systemtap.syscall/alarm.c \
../testsuite/systemtap.syscall/chmod.c \
../testsuite/systemtap.syscall/clock.c \
../testsuite/systemtap.syscall/dir.c \
../testsuite/systemtap.syscall/forkwait.c \
../testsuite/systemtap.syscall/futimes.c \
../testsuite/systemtap.syscall/itimer.c \
../testsuite/systemtap.syscall/link.c \
../testsuite/systemtap.syscall/mmap.c \
../testsuite/systemtap.syscall/mount.c \
../testsuite/systemtap.syscall/net1.c \
../testsuite/systemtap.syscall/openclose.c \
../testsuite/systemtap.syscall/poll.c \
../testsuite/systemtap.syscall/readwrite.c \
../testsuite/systemtap.syscall/rt_signal.c \
../testsuite/systemtap.syscall/select.c \
../testsuite/systemtap.syscall/sendfile.c \
../testsuite/systemtap.syscall/signal.c \
../testsuite/systemtap.syscall/stat.c \
../testsuite/systemtap.syscall/statfs.c \
../testsuite/systemtap.syscall/swap.c \
../testsuite/systemtap.syscall/sync.c \
../testsuite/systemtap.syscall/timer.c \
../testsuite/systemtap.syscall/trunc.c \
../testsuite/systemtap.syscall/uid.c \
../testsuite/systemtap.syscall/uid16.c \
../testsuite/systemtap.syscall/umask.c \
../testsuite/systemtap.syscall/unlink.c 

OBJS += \
./testsuite/systemtap.syscall/access.o \
./testsuite/systemtap.syscall/acct.o \
./testsuite/systemtap.syscall/alarm.o \
./testsuite/systemtap.syscall/chmod.o \
./testsuite/systemtap.syscall/clock.o \
./testsuite/systemtap.syscall/dir.o \
./testsuite/systemtap.syscall/forkwait.o \
./testsuite/systemtap.syscall/futimes.o \
./testsuite/systemtap.syscall/itimer.o \
./testsuite/systemtap.syscall/link.o \
./testsuite/systemtap.syscall/mmap.o \
./testsuite/systemtap.syscall/mount.o \
./testsuite/systemtap.syscall/net1.o \
./testsuite/systemtap.syscall/openclose.o \
./testsuite/systemtap.syscall/poll.o \
./testsuite/systemtap.syscall/readwrite.o \
./testsuite/systemtap.syscall/rt_signal.o \
./testsuite/systemtap.syscall/select.o \
./testsuite/systemtap.syscall/sendfile.o \
./testsuite/systemtap.syscall/signal.o \
./testsuite/systemtap.syscall/stat.o \
./testsuite/systemtap.syscall/statfs.o \
./testsuite/systemtap.syscall/swap.o \
./testsuite/systemtap.syscall/sync.o \
./testsuite/systemtap.syscall/timer.o \
./testsuite/systemtap.syscall/trunc.o \
./testsuite/systemtap.syscall/uid.o \
./testsuite/systemtap.syscall/uid16.o \
./testsuite/systemtap.syscall/umask.o \
./testsuite/systemtap.syscall/unlink.o 

C_DEPS += \
./testsuite/systemtap.syscall/access.d \
./testsuite/systemtap.syscall/acct.d \
./testsuite/systemtap.syscall/alarm.d \
./testsuite/systemtap.syscall/chmod.d \
./testsuite/systemtap.syscall/clock.d \
./testsuite/systemtap.syscall/dir.d \
./testsuite/systemtap.syscall/forkwait.d \
./testsuite/systemtap.syscall/futimes.d \
./testsuite/systemtap.syscall/itimer.d \
./testsuite/systemtap.syscall/link.d \
./testsuite/systemtap.syscall/mmap.d \
./testsuite/systemtap.syscall/mount.d \
./testsuite/systemtap.syscall/net1.d \
./testsuite/systemtap.syscall/openclose.d \
./testsuite/systemtap.syscall/poll.d \
./testsuite/systemtap.syscall/readwrite.d \
./testsuite/systemtap.syscall/rt_signal.d \
./testsuite/systemtap.syscall/select.d \
./testsuite/systemtap.syscall/sendfile.d \
./testsuite/systemtap.syscall/signal.d \
./testsuite/systemtap.syscall/stat.d \
./testsuite/systemtap.syscall/statfs.d \
./testsuite/systemtap.syscall/swap.d \
./testsuite/systemtap.syscall/sync.d \
./testsuite/systemtap.syscall/timer.d \
./testsuite/systemtap.syscall/trunc.d \
./testsuite/systemtap.syscall/uid.d \
./testsuite/systemtap.syscall/uid16.d \
./testsuite/systemtap.syscall/umask.d \
./testsuite/systemtap.syscall/unlink.d 


# Each subdirectory must supply rules for building sources it contributes
testsuite/systemtap.syscall/%.o: ../testsuite/systemtap.syscall/%.c
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C Compiler'
	gcc -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -o"$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


