# Minimum CMake version Zephyr requires
cmake_minimum_required(VERSION 3.20.0)

# Pull in the Zephyr build system. This must come before project().
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

# Name of this project/application
project(tamper_zephyr)

# Tell Zephyr which source files make up the app
target_sources(app PRIVATE src/main.c)