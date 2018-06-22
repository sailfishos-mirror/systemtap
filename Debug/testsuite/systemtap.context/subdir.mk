################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../testsuite/systemtap.context/systemtap_test_module1.c \
../testsuite/systemtap.context/systemtap_test_module2.c 

OBJS += \
./testsuite/systemtap.context/systemtap_test_module1.o \
./testsuite/systemtap.context/systemtap_test_module2.o 

C_DEPS += \
./testsuite/systemtap.context/systemtap_test_module1.d \
./testsuite/systemtap.context/systemtap_test_module2.d 


# Each subdirectory must supply rules for building sources it contributes
testsuite/systemtap.context/%.o: ../testsuite/systemtap.context/%.c
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C Compiler'
	gcc -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -o"$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


