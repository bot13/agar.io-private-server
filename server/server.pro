#-------------------------------------------------
#
# Project created by QtCreator 2015-05-15T14:13:04
#
#-------------------------------------------------

QT       += core
QT       -= gui

QT       += websockets

TARGET = server
CONFIG   += console
CONFIG   -= app_bundle

TEMPLATE = app


SOURCES += main.cpp \
    gameserver.cpp

HEADERS += \
    gameserver.h
