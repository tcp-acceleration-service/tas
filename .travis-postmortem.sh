#!/bin/bash

for f in /tmp/cores/*; do
  if [ ! -f $f ] ; then
    continue
  fi

  echo "Post-mortem for $f"
  gdb -c "$f" \
    -ex "thread apply all bt" \
    -ex "set pagination 0" \
    -batch
  echo "\n\n\n"
done
