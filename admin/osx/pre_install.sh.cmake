#!/bin/sh

# kill the old version. see issue #2044
killall @APPLICATION_EXECUTABLE@
killall @APPLICATION_NAME@

if [ -d "/Applications/@APPLICATION_NAME@.app" ]; then
    echo "Deleting legacy client /Applications/@APPLICATION_NAME@.app"
    sudo rm -rf /Applications/@APPLICATION_NAME@.app
fi

if pkgutil --pkgs=@APPLICATION_REV_DOMAIN@
then
   echo "Running pkgutil --forget @APPLICATION_REV_DOMAIN@"
   sudo pkgutil --forget @APPLICATION_REV_DOMAIN@
fi

exit 0

