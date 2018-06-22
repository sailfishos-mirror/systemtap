################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../runtime/docs/examples/argv.c \
../runtime/docs/examples/foreach.c \
../runtime/docs/examples/list.c \
../runtime/docs/examples/map.c \
../runtime/docs/examples/template.c 

OBJS += \
./runtime/docs/examples/argv.o \
./runtime/docs/examples/foreach.o \
./runtime/docs/examples/list.o \
./runtime/docs/examples/map.o \
./runtime/docs/examples/template.o 

C_DEPS += \
./runtime/docs/examples/argv.d \
./runtime/docs/examples/foreach.d \
./runtime/docs/examples/list.d \
./runtime/docs/examples/map.d \
./runtime/docs/examples/template.d 


# Each subdirectory must supply rules for building sources it contributes
runtime/docs/examples/%.o: ../runtime/docs/examples/%.c
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C Compiler'
	gcc -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -o"$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


