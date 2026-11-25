#!/bin/sh
# Run the job as the caller's uid, so tests do not run as root and the files written to the
# mounted checkout belong to whoever started the container. tools/ci.py passes the ids.
set -eu
uid="${PAPO_UID:-0}"
gid="${PAPO_GID:-0}"
if [ "$uid" = 0 ]; then
    exec "$@"
fi
getent group "$gid" >/dev/null || groupadd --gid "$gid" papo
useradd --uid "$uid" --gid "$gid" --create-home --home-dir /home/papo --shell /bin/bash papo
if [ -d /workspace ]; then
    # Verified fonts and fixtures ship in the image; dist/resources.py then downloads nothing.
    # Jobs started together share the checkout, hence the lock.
    setpriv --reuid "$uid" --regid "$gid" --init-groups sh -eu -c '
        mkdir -p /workspace/build/resources
        flock /workspace/build/.papo-resources.lock \
            cp -a --update=none /opt/papo/resources/. /workspace/build/resources/'
fi
exec setpriv --reuid "$uid" --regid "$gid" --init-groups \
    env HOME=/home/papo USER=papo LOGNAME=papo "$@"
