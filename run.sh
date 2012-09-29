#!/bin/bash -e

#set -x

MCSIGN_DIR="$(dirname "$0")" # Assume it's the directory of this file
MINECRAFT_DIR="$(readlink -f "$MCSIGN_DIR/../minecraft/")"
SIGNS="$MINECRAFT_DIR/.signs"
WORLD_DIR="$MINECRAFT_DIR/world"
DESTINATION="$(readlink -f "$MCSIGN_DIR/../pigmap/output/markers.js")"

###########

changes=$(mktemp /tmp/.mcsign.$$.XXXXXXXX)
ts_file="$MINECRAFT_DIR/.mcsign-stamp"

touch "$ts_file.new"

if [ -e "$ts_file" ]; then
  (cd $WORLD_DIR/region; find -newer "$ts_file") > $changes
else
  (cd $WORLD_DIR/region; find) > $changes
fi

if ! [ -d "$SIGNS" ]; then
  mkdir -p "$SIGNS"
fi

egrep -o 'r\.-?[0-9]*\.-?[0-9]*\.' $changes | cut -d. -f 2,3 | tr . ' ' | "$MCSIGN_DIR/mcsign" "$WORLD_DIR" "$SIGNS"
(
  echo "var markerData = ["
  find "$SIGNS" -name 'signs.*.in' -print0 | xargs -0 cat
  echo "];"
) > "$DESTINATION.tmp"
mv "$DESTINATION.tmp" "$DESTINATION"

mv "$ts_file.new" "$ts_file"
rm -f $changes
