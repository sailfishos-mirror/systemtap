################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../runtime/transport/control.c \
../runtime/transport/procfs.c \
../runtime/transport/relayfs.c \
../runtime/transport/symbols.c \
../runtime/transport/transport.c \
../runtime/transport/utt.c 

OBJS += \
./runtime/transport/control.o \
./runtime/transport/procfs.o \
./runtime/transport/relayfs.o \
./runtime/transport/symbols.o \
./runtime/transport/transport.o \
./runtime/transport/utt.o 

C_DEPS += \
./runtime/transport/control.d \
./runtime/transport/procfs.d \
./runtime/transport/relayfs.d \
./runtime/transport/symbols.d \
./runtime/transport/transport.d \
./runtime/transport/utt.d 


# Each subdirectory must supply rules for building sources it contributes
runtime/transport/%.o: ../runtime/transport/%.c
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C Compiler'
	gcc -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -o"$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


