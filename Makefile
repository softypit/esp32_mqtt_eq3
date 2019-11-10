#
# This is a project Makefile. It is assumed the directory this Makefile resides in is a
# project subdirectory.
#
ifndef IDF_PATH
  $(error "This needs an installed Espressiv IDF + IDF_PATH variable set to point to it")
endif

PROJECT_NAME := eq3_trv_control
PROJECT_VER := 1.49-beta

COMPONENT_ADD_INCLUDEDIRS := components/include

include $(IDF_PATH)/make/project.mk
