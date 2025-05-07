QT += core gui
QT += network

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++11

# 添加C语言支持
CONFIG += c
QMAKE_CFLAGS += -std=c11
QMAKE_CFLAGS += -D_DEFAULT_SOURCE

DEFINES += QT_DEPRECATED_WARNINGS

SOURCES += \
    main.cpp \
    $$files(src/*.cpp, true) \
    $$files(src/*.c, true) \
    extern/sqlite/sqlite3.c

HEADERS += \
    $$files(include/*.hpp, true) \
    $$files(include/*.h, true) \
    extern/sqlite/sqlite3.h

FORMS += \
    chat_window.ui

# 包含目录设置
INCLUDEPATH += include
INCLUDEPATH += $$PWD/extern/openssl-3.0.14/include
INCLUDEPATH += $$PWD/extern/jpeg-9e
INCLUDEPATH += $$PWD/extern/sqlite

# 库路径和链接
LIBS += -L$$PWD/extern/openssl-3.0.14 \
        -lssl \
        -lcrypto

LIBS += -L$$PWD/extern/jpeg-9e/.libs -ljpeg

# 静态库需要添加依赖库（根据实际需要）
unix:!macx: LIBS += -ldl -lpthread

# 构建目录设置
DESTDIR = build
OBJECTS_DIR = build/obj
MOC_DIR = build/moc
RCC_DIR = build/rcc
UI_DIR = build/ui

# 部署规则
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
