#!/bin/sh

# kill the old version. see issue #2044
killall @APPLICATION_EXECUTABLE@
killall @APPLICATION_NAME@
sudo rm -rf /Applications/@APPLICATION_NAME@.app

exit 0
