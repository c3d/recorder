# *****************************************************************************
#  recorder_scope.pro                                  Flight recorder project
# *****************************************************************************
#
#   File Description:
#
#     Project for the flight recorder "scope"
#
#
#
#
#
#
#
#
# *****************************************************************************
#  (C) 2017-2018 Christophe de Dinechin <christophe@dinechin.org>
#   This software is licensed under the GNU General Public License v3+
#   See LICENSE file for details.
# *****************************************************************************

QT += charts

INCLUDEPATH += ..

HEADERS += recorder_view.h

SOURCES += recorder_view.cpp    \
           recorder_scope.cpp   \
           recorder_slider.cpp

LIBS += -L.. -lrecorder

target.files = recorder_scope
target.path = $$INSTALL_BINDIR
INSTALLS += target
