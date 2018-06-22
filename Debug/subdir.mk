################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../loc2c-test.c \
../loc2c.c \
../mdfour.c \
../staplog.c 

CXX_SRCS += \
../buildrun.cxx \
../cache.cxx \
../coveragedb.cxx \
../dwarf_wrappers.cxx \
../elaborate.cxx \
../hash.cxx \
../main.cxx \
../parse.cxx \
../staptree.cxx \
../tapsets.cxx \
../translate.cxx \
../util.cxx 

OBJS += \
./buildrun.o \
./cache.o \
./coveragedb.o \
./dwarf_wrappers.o \
./elaborate.o \
./hash.o \
./loc2c-test.o \
./loc2c.o \
./main.o \
./mdfour.o \
./parse.o \
./staplog.o \
./staptree.o \
./tapsets.o \
./translate.o \
./util.o 

C_DEPS += \
./loc2c-test.d \
./loc2c.d \
./mdfour.d \
./staplog.d 

CXX_DEPS += \
./buildrun.d \
./cache.d \
./coveragedb.d \
./dwarf_wrappers.d \
./elaborate.d \
./hash.d \
./main.d \
./parse.d \
./staptree.d \
./tapsets.d \
./translate.d \
./util.d 


# Each subdirectory must supply rules for building sources it contributes
%.o: ../%.cxx
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C++ Compiler'
	g++ -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -o"$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

%.o: ../%.c
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C Compiler'
	gcc -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -o"$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


