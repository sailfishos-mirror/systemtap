################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../runtime/bench2/itest.c 

OBJS += \
./runtime/bench2/itest.o 

C_DEPS += \
./runtime/bench2/itest.d 


# Each subdirectory must supply rules for building sources it contributes
runtime/bench2/%.o: ../runtime/bench2/%.c
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C Compiler'
	gcc -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -o"$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


