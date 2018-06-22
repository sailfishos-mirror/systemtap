################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../runtime/uprobes/uprobes.c \
../runtime/uprobes/uprobes_arch.c \
../runtime/uprobes/uprobes_i386.c \
../runtime/uprobes/uprobes_ppc64.c \
../runtime/uprobes/uprobes_s390.c \
../runtime/uprobes/uprobes_x86.c \
../runtime/uprobes/uprobes_x86_64.c 

OBJS += \
./runtime/uprobes/uprobes.o \
./runtime/uprobes/uprobes_arch.o \
./runtime/uprobes/uprobes_i386.o \
./runtime/uprobes/uprobes_ppc64.o \
./runtime/uprobes/uprobes_s390.o \
./runtime/uprobes/uprobes_x86.o \
./runtime/uprobes/uprobes_x86_64.o 

C_DEPS += \
./runtime/uprobes/uprobes.d \
./runtime/uprobes/uprobes_arch.d \
./runtime/uprobes/uprobes_i386.d \
./runtime/uprobes/uprobes_ppc64.d \
./runtime/uprobes/uprobes_s390.d \
./runtime/uprobes/uprobes_x86.d \
./runtime/uprobes/uprobes_x86_64.d 


# Each subdirectory must supply rules for building sources it contributes
runtime/uprobes/%.o: ../runtime/uprobes/%.c
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C Compiler'
	gcc -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -o"$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


