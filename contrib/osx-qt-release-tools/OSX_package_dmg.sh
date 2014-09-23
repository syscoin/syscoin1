#!/bin/bash
#THIS SCRIPT SHOULD BE RUN AS ROOT!!!

#rename port-based db48 dir pre-packaging
mv /opt/local/lib/db48 /opt/local/lib/lib

#run macdeploy without the DMG command so we can fix the db48 linker manually
macdeployqt Syscoin-Qt.app/ -verbose=3

#rename port-based db48 to the right name
mv /opt/local/lib/lib /opt/local/lib/db48

#fix linker ids
install_name_tool -id @executable_path/../Frameworks/libdb_cxx-4.8.dylib "Syscoin-Qt.app/Contents/MacOS/Syscoin-Qt"
install_name_tool -change /opt/local/lib/db48/libdb_cxx-4.8.dylib @executable_path/../Frameworks/libdb_cxx-4.8.dylib "Syscoin-Qt.app/Contents/MacOS/Syscoin-Qt"

#package the final product into a DMG
macdeployqt Syscoin-Qt.app/ -dmg

#end