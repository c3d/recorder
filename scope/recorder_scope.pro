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
#  (C) 2017 Christophe de Dinechin <christophe@dinechin.org>
#   This software is licensed under the GNU General Public License v3
#   See LICENSE file for details.
# *****************************************************************************

QT += charts

INCPATH += ..

HEADERS += recorder_view.h

SOURCES += recorder_view.cpp    \
           recorder_scope.cpp   \
           recorder_slider.cpp  \
           ../recorder.c        \
           ../recorder_ring.c
