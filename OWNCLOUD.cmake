set( APPLICATION_NAME       "WebDAV Sync" )
set( APPLICATION_SHORTNAME  "webdav-sync" )
set( APPLICATION_EXECUTABLE "davcloud" )
set( APPLICATION_DOMAIN     "webdav.org" )
set( APPLICATION_VENDOR     "ownCloud" )
set( APPLICATION_UPDATE_URL "none://." CACHE string "URL for updater" )
set( APPLICATION_ICON_NAME  "davcloud" )

set( LINUX_PACKAGE_SHORTNAME "davcloud" )

set( THEME_CLASS            "ownCloudTheme" )
set( APPLICATION_REV_DOMAIN "org.webdav.syncclient" )
set( WIN_SETUP_BITMAP_PATH  "${CMAKE_SOURCE_DIR}/admin/win/nsi" )

set( MAC_INSTALLER_BACKGROUND_FILE "${CMAKE_SOURCE_DIR}/admin/osx/installer-background.png" CACHE STRING "The MacOSX installer background image")

# set( THEME_INCLUDE          "${OEM_THEME_DIR}/mytheme.h" )
# set( APPLICATION_LICENSE    "${OEM_THEME_DIR}/license.txt )

option( WITH_CRASHREPORTER "Build crashreporter" OFF )
set( CRASHREPORTER_SUBMIT_URL "none://." CACHE string "URL for crash reporter" )
set( CRASHREPORTER_ICON ":/owncloud-icon.png" )
