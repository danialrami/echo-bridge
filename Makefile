# Add the unicode.c file to the Makefile
CPP_SOURCES = EchoBridgeWithUSB.cpp hothouse.cpp
C_SOURCES = 
C_INCLUDES = -I. -I$(LIBDAISY_DIR) -I$(DAISYSP_DIR)
C_SOURCES += unicode.c

TARGET = EchoBridge

# Library Locations
LIBDAISY_DIR = /home/ubuntu/DaisyExamples/libDaisy
DAISYSP_DIR = /home/ubuntu/DaisyExamples/DaisySP

# Core location, and generic Makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
