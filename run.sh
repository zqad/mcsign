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
  find "$WORLD_DIR/region" -maxdepth 1 -type f -name '*.mca' -newer "$ts_file" -print0 > $changes
else
  find "$WORLD_DIR/region" -maxdepth 1 -type f -name '*.mca' -print0 > $changes
fi

if ! [ -d "$SIGNS" ]; then
  mkdir -p "$SIGNS"
fi

"$MCSIGN_DIR/mcsign" -o "$SIGNS" -0 < $changes
(
  echo "var markerData = ["
  find "$SIGNS" -name 'signs.*.in' -print0 | xargs -0 cat
  echo "];"
) > "$DESTINATION.tmp"
mv "$DESTINATION.tmp" "$DESTINATION"

mv "$ts_file.new" "$ts_file"
rm -f $changes
