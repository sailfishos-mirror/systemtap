################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../runtime/staprun/common.c \
../runtime/staprun/ctl.c \
../runtime/staprun/mainloop.c \
../runtime/staprun/relay.c \
../runtime/staprun/relay_old.c \
../runtime/staprun/stap_merge.c \
../runtime/staprun/stapio.c \
../runtime/staprun/staprun.c \
../runtime/staprun/staprun_funcs.c 

OBJS += \
./runtime/staprun/common.o \
./runtime/staprun/ctl.o \
./runtime/staprun/mainloop.o \
./runtime/staprun/relay.o \
./runtime/staprun/relay_old.o \
./runtime/staprun/stap_merge.o \
./runtime/staprun/stapio.o \
./runtime/staprun/staprun.o \
./runtime/staprun/staprun_funcs.o 

C_DEPS += \
./runtime/staprun/common.d \
./runtime/staprun/ctl.d \
./runtime/staprun/mainloop.d \
./runtime/staprun/relay.d \
./runtime/staprun/relay_old.d \
./runtime/staprun/stap_merge.d \
./runtime/staprun/stapio.d \
./runtime/staprun/staprun.d \
./runtime/staprun/staprun_funcs.d 


# Each subdirectory must supply rules for building sources it contributes
runtime/staprun/%.o: ../runtime/staprun/%.c
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C Compiler'
	gcc -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -o"$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


