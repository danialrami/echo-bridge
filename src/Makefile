# Makefile for Echo Bridge
# Optimized for Daisy Seed with 64MB SDRAM
# Project Name
TARGET = EchoBridge
# Sources
CPP_SOURCES = EchoBridgeWithUSB.cpp hothouse.cpp
C_SOURCES = unicode.c
# Library Locations
LIBDAISY_DIR = /home/ubuntu/DaisyExamples/libDaisy
DAISYSP_DIR = /home/ubuntu/DaisyExamples/DaisySP
# Optimization flag - optimize for size
OPT = -Os
# Core location, and generic Makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
