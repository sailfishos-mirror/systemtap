################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../testsuite/systemtap.base/utrace_p5_multi.c 

OBJS += \
./testsuite/systemtap.base/utrace_p5_multi.o 

C_DEPS += \
./testsuite/systemtap.base/utrace_p5_multi.d 


# Each subdirectory must supply rules for building sources it contributes
testsuite/systemtap.base/%.o: ../testsuite/systemtap.base/%.c
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C Compiler'
	gcc -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -o"$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


